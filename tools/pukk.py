#!/usr/bin/env python3
"""Pukk pack tool: build .pukk packs from JSON and manage them over BLE.

Format and protocol: docs/PACKS.md. The BLE commands need `bleak`
(pip install -r tools/requirements.txt).

    python3 tools/pukk.py build packs/voorbeeld.json
    python3 tools/pukk.py inspect packs/voorbeeld.pukk
    python3 tools/pukk.py scan
    python3 tools/pukk.py upload packs/voorbeeld.json [--device Pukk-EA66]
    python3 tools/pukk.py list
    python3 tools/pukk.py delete voorbeeld
"""
from __future__ import annotations

import argparse
import asyncio
import json
import re
import struct
import sys
import unicodedata
import zlib
from pathlib import Path

MAGIC = b"PUKK"
VERSION = 1
HEADER = struct.Struct("<4sBBHIIIIII")    # 32 bytes
QUESTION = struct.Struct("<I4IBBBB")       # 24 bytes
NO_STRING = 0xFFFFFFFF

MAX_FILE_SIZE = 1024 * 1024
MAX_QUESTIONS = 1000
TITLE_MAX = 40
QUESTION_MAX = 120
ANSWER_MAX = 32
DEFAULT_TIME = 15
MIN_TIME, MAX_TIME = 5, 60

# Punctuation that NFKD does not reduce to ASCII.
ASCII_REPLACEMENTS = {
    "‘": "'", "’": "'", "“": '"', "”": '"',
    "–": "-", "—": "-", "…": "...", " ": " ",
}

NAME_RE = re.compile(r"^[a-z0-9_-]{1,32}$")

SERVICE_UUID = "8e410001-0580-4871-83bd-51273ef66407"
CTRL_UUID, DATA_UUID, INFO_UUID, PACKS_UUID = (
    f"8e41{n:04x}-0580-4871-83bd-51273ef66407" for n in (2, 3, 4, 5))

OP_BEGIN, OP_END, OP_ABORT, OP_DELETE, OP_DATA = 0x01, 0x02, 0x03, 0x04, 0x10
RSP_OK, RSP_PROGRESS, RSP_RESEND, RSP_ERROR = 0x80, 0x81, 0x82, 0x8F

UPLOAD_WINDOW = 8 * 1024    # unacknowledged bytes in flight
UPLOAD_STALL_S = 2.0        # no progress for this long: resend from the last ack
UPLOAD_MAX_STALLS = 5
DEVICE_ERRORS = {
    1: "busy: another upload is in progress",
    2: "invalid request",
    3: "not enough space on the device",
    4: "no upload active",
    5: "data arrived out of order",
    6: "size or CRC mismatch",
    7: "not a valid pack",
    8: "read/write error on the device",
    9: "not found",
}


class PackError(Exception):
    pass


def to_ascii(text: str, where: str, warnings: list[str]) -> str:
    """Until the device has a font with accents (phase 2), strip them."""
    for src, dst in ASCII_REPLACEMENTS.items():
        text = text.replace(src, dst)
    if text.isascii():
        return text
    stripped = "".join(c for c in unicodedata.normalize("NFKD", text)
                       if not unicodedata.combining(c))
    if not stripped.isascii():
        bad = "".join(sorted({c for c in stripped if not c.isascii()}))
        raise PackError(f"{where}: characters the device cannot show: {bad!r}")
    warnings.append(f"{where}: {text!r} -> {stripped!r}")
    return stripped


class StringTable:
    def __init__(self) -> None:
        self.data = bytearray()
        self.offsets: dict[str, int] = {}

    def add(self, text: str) -> int:
        if text not in self.offsets:
            self.offsets[text] = len(self.data)
            self.data += text.encode("utf-8") + b"\0"
        return self.offsets[text]


def check_text(value, where: str, limit: int, warnings: list[str]) -> str:
    if not isinstance(value, str) or not value.strip():
        raise PackError(f"{where}: must be a non-empty string")
    text = to_ascii(value.strip(), where, warnings)
    if len(text.encode("utf-8")) > limit:
        raise PackError(f"{where}: longer than {limit} bytes: {text!r}")
    return text


def build_pack(src: dict, warnings: list[str]) -> bytes:
    if not isinstance(src, dict):
        raise PackError("top level must be an object")
    questions = src.get("questions")
    if not isinstance(questions, list) or not questions:
        raise PackError("'questions' must be a non-empty list")
    if len(questions) > MAX_QUESTIONS:
        raise PackError(f"at most {MAX_QUESTIONS} questions")

    strings = StringTable()
    title = strings.add(check_text(src.get("title"), "title", TITLE_MAX, warnings))

    entries = bytearray()
    for n, q in enumerate(questions, start=1):
        where = f"question {n}"
        if not isinstance(q, dict):
            raise PackError(f"{where}: must be an object")
        text = check_text(q.get("question"), where, QUESTION_MAX, warnings)
        answers = q.get("answers")
        if not isinstance(answers, list) or not 2 <= len(answers) <= 4:
            raise PackError(f"{where}: 'answers' must have 2 to 4 items")
        answers = [check_text(a, f"{where} answer {i + 1}", ANSWER_MAX, warnings)
                   for i, a in enumerate(answers)]
        correct = q.get("correct")
        if not isinstance(correct, int) or not 0 <= correct < len(answers):
            raise PackError(f"{where}: 'correct' must be an index into 'answers'")
        time_s = q.get("time", DEFAULT_TIME)
        if not isinstance(time_s, int) or not MIN_TIME <= time_s <= MAX_TIME:
            raise PackError(f"{where}: 'time' must be {MIN_TIME}..{MAX_TIME} seconds")

        offsets = [strings.add(a) for a in answers]
        offsets += [NO_STRING] * (4 - len(offsets))
        entries += QUESTION.pack(strings.add(text), *offsets, len(answers), correct,
                                 0 if time_s == DEFAULT_TIME else time_s, 0)

    index_offset = HEADER.size
    strings_offset = index_offset + len(entries)
    file_size = strings_offset + len(strings.data)
    if file_size > MAX_FILE_SIZE:
        raise PackError(f"pack is {file_size} bytes, maximum is {MAX_FILE_SIZE}")

    def header(crc: int) -> bytes:
        return HEADER.pack(MAGIC, VERSION, 0, len(questions), file_size, crc,
                           index_offset, strings_offset, len(strings.data), title)

    body = bytes(entries) + bytes(strings.data)
    crc = zlib.crc32(header(0) + body)
    return header(crc) + body


def read_string(table: bytes, offset: int) -> str:
    end = table.index(b"\0", offset)
    return table[offset:end].decode("utf-8")


def inspect_pack(data: bytes) -> dict:
    """Parse a .pukk file back into the JSON source shape (for checking)."""
    (magic, version, _flags, count, file_size, crc, index_offset, strings_offset,
     strings_size, title) = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or file_size != len(data):
        raise PackError("not a valid .pukk v1 file")
    zeroed = data[:12] + b"\0\0\0\0" + data[16:]
    if zlib.crc32(zeroed) != crc:
        raise PackError("CRC mismatch")
    table = data[strings_offset:strings_offset + strings_size]
    out = {"title": read_string(table, title), "questions": []}
    for i in range(count):
        text, *rest = QUESTION.unpack_from(data, index_offset + i * QUESTION.size)
        answer_offsets, (n_answers, correct, time_s, _) = rest[:4], rest[4:]
        out["questions"].append({
            "question": read_string(table, text),
            "answers": [read_string(table, o) for o in answer_offsets[:n_answers]],
            "correct": correct,
            "time": time_s or DEFAULT_TIME,
        })
    return out


def load_pack(path: Path) -> tuple[bytes, list[str]]:
    """Return .pukk bytes for a .json source or an already built .pukk file."""
    warnings: list[str] = []
    if path.suffix == ".pukk":
        return path.read_bytes(), warnings
    try:
        src = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise PackError(f"{path}: invalid JSON: {e}") from e
    return build_pack(src, warnings), warnings


def pack_name(path: Path, name: str | None) -> str:
    name = name or path.stem.lower().replace(" ", "-")
    if not NAME_RE.match(name):
        raise PackError(f"pack name {name!r}: use 1-32 of a-z 0-9 - _ (or pass --name)")
    return name


def cmd_build(args: argparse.Namespace) -> None:
    data, warnings = load_pack(args.source)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    out = args.output or args.source.with_suffix(".pukk")
    out.write_bytes(data)
    count = struct.unpack_from("<H", data, 6)[0]
    print(f"{out}: {count} questions, {len(data)} bytes, crc32 {zlib.crc32(data):08x}")


def cmd_inspect(args: argparse.Namespace) -> None:
    print(json.dumps(inspect_pack(args.pack.read_bytes()), indent=2, ensure_ascii=False))


# --- BLE ---------------------------------------------------------------------

def require_bleak():
    try:
        import bleak  # noqa: F401
    except ImportError as e:
        raise PackError("BLE commands need bleak: pip install -r tools/requirements.txt") from e


async def find_device(name: str | None, timeout: float):
    from bleak import BleakScanner

    def match(device, adv) -> bool:
        local = adv.local_name or device.name or ""
        if name:
            return local == name
        return local.startswith("Pukk-") or SERVICE_UUID in (u.lower() for u in adv.service_uuids)

    device = await BleakScanner.find_device_by_filter(match, timeout=timeout)
    if device is None:
        raise PackError(f"no {name or 'Pukk device'} found within {timeout:.0f} s")
    return device


class Link:
    """Requests on CTRL and the notifications that answer them."""

    def __init__(self, client) -> None:
        self.client = client
        self.queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.received = 0               # last PROGRESS from the device
        self.resend_from: int | None = None  # set by RESEND

    async def start(self) -> None:
        await self.client.start_notify(CTRL_UUID, lambda _c, data: self.queue.put_nowait(bytes(data)))

    def _handle(self, msg: bytes, op: int) -> bytes | None:
        if msg[0] == RSP_PROGRESS and len(msg) >= 5:
            self.received = max(self.received, struct.unpack_from("<I", msg, 1)[0])
        elif msg[0] == RSP_RESEND and len(msg) >= 5:
            self.resend_from = struct.unpack_from("<I", msg, 1)[0]
            self.received = max(self.received, self.resend_from)
        elif msg[0] == RSP_ERROR and len(msg) >= 3:
            raise PackError(f"device: {DEVICE_ERRORS.get(msg[2], f'error {msg[2]}')}")
        elif msg[0] == RSP_OK and len(msg) >= 2 and msg[1] == op:
            return msg
        return None

    async def request(self, payload: bytes, timeout: float = 10.0) -> bytes:
        await self.client.write_gatt_char(CTRL_UUID, payload, response=True)
        while True:
            msg = await asyncio.wait_for(self.queue.get(), timeout)
            if (rsp := self._handle(msg, payload[0])) is not None:
                return rsp

    def poll(self) -> None:
        """Handle notifications that arrived meanwhile (progress, errors)."""
        while not self.queue.empty():
            self._handle(self.queue.get_nowait(), -1)

    async def wait_notification(self, timeout: float) -> bool:
        """Handle the next notification; False if none came within `timeout`."""
        try:
            msg = await asyncio.wait_for(self.queue.get(), timeout)
        except (TimeoutError, asyncio.TimeoutError):
            return False
        self._handle(msg, -1)
        return True


async def with_device(args: argparse.Namespace, action):
    from bleak import BleakClient
    device = await find_device(args.device, args.timeout)
    print(f"connecting to {device.name or device.address}...", file=sys.stderr)
    async with BleakClient(device) as client:
        return await action(client)


def progress_bar(done: int, total: int) -> None:
    width = 30
    filled = width * done // total
    print(f"\r[{'#' * filled}{'.' * (width - filled)}] {done}/{total} bytes", end="",
          file=sys.stderr, flush=True)


async def wait_until_can_send(client) -> None:
    """macOS: CoreBluetooth drops writes without response when its queue is
    full, and bleak does not wait for room. Poll the flag ourselves."""
    peripheral = getattr(getattr(client, "_backend", None), "_peripheral", None)
    can_send = getattr(peripheral, "canSendWriteWithoutResponse", None)
    if can_send is None:
        return
    for _ in range(200):
        if can_send():
            return
        await asyncio.sleep(0.005)


async def upload(client, data: bytes, name: str) -> None:
    """Send `data` with at most UPLOAD_WINDOW bytes unacknowledged. Lost writes
    are recovered from RESEND, or after a stall from the last PROGRESS."""
    link = Link(client)
    await link.start()
    begin = struct.pack("<BIIB", OP_BEGIN, len(data), zlib.crc32(data), len(name)) + name.encode()
    rsp = await link.request(begin)
    chunk = struct.unpack_from("<H", rsp, 2)[0]
    if client.mtu_size:
        chunk = min(chunk, client.mtu_size - 3 - 4)

    try:
        offset = 0
        stalls = 0
        while link.received < len(data):
            link.poll()
            if link.resend_from is not None:
                offset, link.resend_from = link.resend_from, None
            if offset < len(data) and offset - link.received < UPLOAD_WINDOW:
                await wait_until_can_send(client)
                payload = struct.pack("<I", offset) + data[offset:offset + chunk]
                await client.write_gatt_char(DATA_UUID, payload, response=False)
                offset += len(payload) - 4
                progress_bar(link.received, len(data))
                continue
            # Window full or everything sent: wait for the device to catch up.
            if await link.wait_notification(UPLOAD_STALL_S):
                stalls = 0
            else:
                stalls += 1
                if stalls > UPLOAD_MAX_STALLS:
                    raise PackError("upload stalled")
                offset = link.received
            progress_bar(link.received, len(data))
        print(file=sys.stderr)
        await link.request(bytes([OP_END]), timeout=30.0)
    except BaseException:
        print(file=sys.stderr)
        if client.is_connected:
            try:
                await client.write_gatt_char(CTRL_UUID, bytes([OP_ABORT]), response=True)
            except Exception:
                pass
        raise


def cmd_scan(args: argparse.Namespace) -> None:
    require_bleak()
    from bleak import BleakScanner

    async def scan():
        found = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
        for device, adv in found.values():
            local = adv.local_name or device.name or ""
            if local.startswith("Pukk-") or SERVICE_UUID in (u.lower() for u in adv.service_uuids):
                print(f"{local or '?':12} {device.address}  RSSI {adv.rssi} dBm")

    asyncio.run(scan())


def cmd_info(args: argparse.Namespace) -> None:
    require_bleak()

    async def action(client):
        return json.loads(await client.read_gatt_char(INFO_UUID))

    print(json.dumps(asyncio.run(with_device(args, action)), indent=2))


def cmd_list(args: argparse.Namespace) -> None:
    require_bleak()

    async def action(client):
        return json.loads(await client.read_gatt_char(PACKS_UUID))

    packs = asyncio.run(with_device(args, action))
    if not packs:
        print("no packs on the device")
    for p in packs:
        print(f"{p['name']:20} {p['questions']:4} questions {p['size']:8} bytes  {p['title']}")


def cmd_upload(args: argparse.Namespace) -> None:
    require_bleak()
    data, warnings = load_pack(args.pack)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    inspect_pack(data)  # refuse to send something the device would reject
    name = pack_name(args.pack, args.name)

    async def action(client):
        await upload(client, data, name)

    asyncio.run(with_device(args, action))
    print(f"uploaded {name} ({len(data)} bytes)")


def cmd_delete(args: argparse.Namespace) -> None:
    require_bleak()
    if not NAME_RE.match(args.name):
        raise PackError(f"invalid pack name {args.name!r}")

    async def action(client):
        link = Link(client)
        await link.start()
        name = args.name.encode()
        await link.request(bytes([OP_DELETE, len(name)]) + name)

    asyncio.run(with_device(args, action))
    print(f"deleted {args.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build", help="convert a JSON pack to .pukk")
    p.add_argument("source", type=Path)
    p.add_argument("-o", "--output", type=Path)
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("inspect", help="check a .pukk file and print it as JSON")
    p.add_argument("pack", type=Path)
    p.set_defaults(func=cmd_inspect)

    def ble_command(name: str, help: str):
        p = sub.add_parser(name, help=help)
        p.add_argument("--device", help="device name, e.g. Pukk-EA66 (default: first found)")
        p.add_argument("--timeout", type=float, default=10.0, help="scan time in seconds")
        return p

    ble_command("scan", "list Pukk devices in range").set_defaults(func=cmd_scan)
    ble_command("info", "show firmware version and free flash").set_defaults(func=cmd_info)
    ble_command("list", "list the packs on a device").set_defaults(func=cmd_list)
    p = ble_command("upload", "upload a pack (.json or .pukk)")
    p.add_argument("pack", type=Path)
    p.add_argument("--name", help="pack name on the device (default: file name)")
    p.set_defaults(func=cmd_upload)
    p = ble_command("delete", "delete a pack from a device")
    p.add_argument("name")
    p.set_defaults(func=cmd_delete)

    args = parser.parse_args()
    try:
        args.func(args)
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except (TimeoutError, asyncio.TimeoutError):
        print("error: no response from the device", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
