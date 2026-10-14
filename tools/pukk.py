#!/usr/bin/env python3
"""Pukk pack tool: build .pukk packs from JSON and inspect them.

Format: docs/PACKS.md.

    python3 tools/pukk.py build packs/voorbeeld.json
    python3 tools/pukk.py inspect packs/voorbeeld.pukk
"""
from __future__ import annotations

import argparse
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

    args = parser.parse_args()
    try:
        args.func(args)
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
