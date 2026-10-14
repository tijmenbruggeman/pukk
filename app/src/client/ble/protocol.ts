/*
 * Pukk BLE protocol (docs/PACKS.md), independent of Web Bluetooth so it can
 * be tested against a simulated device. Mirrors the upload logic of
 * tools/pukk.py: an 8 KiB window, RESEND on gaps, resume after a stall, END
 * only after the device has confirmed every byte.
 */
import { crc32 } from '../../shared/pack.ts';

export const SERVICE_UUID = '8e410001-0580-4871-83bd-51273ef66407';
const uuid = (n: number) => `8e41${n.toString(16).padStart(4, '0')}-0580-4871-83bd-51273ef66407`;
export const CTRL_UUID = uuid(2);
export const DATA_UUID = uuid(3);
export const INFO_UUID = uuid(4);
export const PACKS_UUID = uuid(5);

export const OP = { BEGIN: 0x01, END: 0x02, ABORT: 0x03, DELETE: 0x04, DATA: 0x10 } as const;
export const RSP = { OK: 0x80, PROGRESS: 0x81, RESEND: 0x82, ERROR: 0x8f } as const;

const DEVICE_ERRORS: Record<number, string> = {
  1: 'Pukk is al een pack aan het ontvangen',
  2: 'Ongeldig verzoek',
  3: 'Niet genoeg ruimte op de Pukk',
  4: 'Geen upload bezig',
  5: 'Data niet op volgorde',
  6: 'Data beschadigd onderweg',
  7: 'Geen geldig pack',
  8: 'Lees- of schrijffout op de Pukk',
  9: 'Niet gevonden',
};

export class DeviceError extends Error {
  readonly code: number;
  constructor(code: number) {
    super(DEVICE_ERRORS[code] ?? `Fout ${code} op de Pukk`);
    this.code = code;
  }
}

/** The GATT operations the protocol needs; see webbluetooth.ts. */
export interface PukkTransport {
  writeCtrl(data: Uint8Array): Promise<void>;
  writeData(data: Uint8Array): Promise<void>;
  read(characteristic: 'info' | 'packs'): Promise<Uint8Array>;
  /** Receive CTRL notifications; returns a function that unsubscribes. */
  subscribe(listener: (data: Uint8Array) => void): () => void;
}

export interface DeviceInfo {
  fw: string;
  flash_total: number;
  flash_free: number;
}

export interface DevicePack {
  name: string;
  title: string;
  questions: number;
  size: number;
}

export interface UploadOptions {
  window?: number;
  stallMs?: number;
  maxStalls?: number;
  responseMs?: number;
  onProgress?: (sent: number, total: number) => void;
}

const decoder = new TextDecoder();

export async function readInfo(t: PukkTransport): Promise<DeviceInfo> {
  return JSON.parse(decoder.decode(await t.read('info')));
}

export async function readPacks(t: PukkTransport): Promise<DevicePack[]> {
  return JSON.parse(decoder.decode(await t.read('packs')));
}

function u32(data: Uint8Array, offset: number): number {
  return new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(offset, true);
}

/** CTRL notifications for the duration of one operation. */
class Session {
  received = 0;
  resendFrom: number | null = null;
  error: DeviceError | null = null;
  private responses: Uint8Array[] = [];
  private wake: (() => void) | null = null;
  private unsubscribe: () => void;
  private t: PukkTransport;

  constructor(t: PukkTransport) {
    this.t = t;
    this.unsubscribe = t.subscribe((msg) => this.onMessage(msg));
  }

  close(): void {
    this.unsubscribe();
  }

  private onMessage(msg: Uint8Array): void {
    if (msg[0] === RSP.PROGRESS && msg.length >= 5) {
      this.received = Math.max(this.received, u32(msg, 1));
    } else if (msg[0] === RSP.RESEND && msg.length >= 5) {
      this.resendFrom = u32(msg, 1);
      this.received = Math.max(this.received, this.resendFrom);
    } else if (msg[0] === RSP.ERROR && msg.length >= 3) {
      this.error = new DeviceError(msg[2]);
    } else if (msg[0] === RSP.OK && msg.length >= 2) {
      this.responses.push(msg);
    }
    const wake = this.wake;
    this.wake = null;
    wake?.();
  }

  throwIfError(): void {
    if (this.error) {
      throw this.error;
    }
  }

  /** Resolves on the next notification, or with false after `ms`. */
  next(ms: number): Promise<boolean> {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.wake = null;
        resolve(false);
      }, ms);
      this.wake = () => {
        clearTimeout(timer);
        resolve(true);
      };
    });
  }

  async request(payload: Uint8Array, ms: number): Promise<Uint8Array> {
    await this.t.writeCtrl(payload);
    const deadline = Date.now() + ms;
    for (;;) {
      this.throwIfError();
      const i = this.responses.findIndex((r) => r[1] === payload[0]);
      if (i >= 0) {
        return this.responses.splice(i, 1)[0];
      }
      const left = deadline - Date.now();
      if (left <= 0 || !(await this.next(left))) {
        this.throwIfError();
        throw new Error('Pukk antwoordt niet');
      }
    }
  }
}

function nameBytes(name: string): Uint8Array {
  const bytes = new TextEncoder().encode(name);
  return Uint8Array.of(bytes.length, ...bytes);
}

export async function deletePack(t: PukkTransport, name: string): Promise<void> {
  const s = new Session(t);
  try {
    await s.request(Uint8Array.of(OP.DELETE, ...nameBytes(name)), 10_000);
  } finally {
    s.close();
  }
}

/** Upload `data` as /packs/<name>.pukk; replaces a pack with that name. */
export async function uploadPack(t: PukkTransport, name: string, data: Uint8Array,
  opts: UploadOptions = {}): Promise<void> {
  const window = opts.window ?? 8 * 1024;
  const stallMs = opts.stallMs ?? 2000;
  const maxStalls = opts.maxStalls ?? 5;
  const responseMs = opts.responseMs ?? 10_000;

  const s = new Session(t);
  try {
    const begin = new Uint8Array(9);
    const bv = new DataView(begin.buffer);
    begin[0] = OP.BEGIN;
    bv.setUint32(1, data.length, true);
    bv.setUint32(5, crc32(data), true);
    const rsp = await s.request(Uint8Array.of(...begin, ...nameBytes(name)), responseMs);
    const chunk = rsp[2] | (rsp[3] << 8);

    try {
      let offset = 0;
      let stalls = 0;
      while (s.received < data.length) {
        s.throwIfError();
        if (s.resendFrom !== null) {
          offset = s.resendFrom;
          s.resendFrom = null;
        }
        if (offset < data.length && offset - s.received < window) {
          const part = data.subarray(offset, offset + chunk);
          const packet = new Uint8Array(4 + part.length);
          new DataView(packet.buffer).setUint32(0, offset, true);
          packet.set(part, 4);
          await t.writeData(packet);
          offset += part.length;
          opts.onProgress?.(s.received, data.length);
          continue;
        }
        // Window full or everything sent: wait for the device to catch up.
        if (await s.next(stallMs)) {
          stalls = 0;
        } else if (++stalls > maxStalls) {
          throw new Error('Upload loopt vast');
        } else {
          offset = s.received;
        }
        opts.onProgress?.(s.received, data.length);
      }
      await s.request(Uint8Array.of(OP.END), 30_000);
      opts.onProgress?.(data.length, data.length);
    } catch (e) {
      await t.writeCtrl(Uint8Array.of(OP.ABORT)).catch(() => {});
      throw e;
    }
  } finally {
    s.close();
  }
}
