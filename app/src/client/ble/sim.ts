/*
 * A simulated Pukk for tests: implements the device side of the protocol the
 * same way as components/ble/protocol.c and components/packs/packs.c, and can
 * drop DATA writes like CoreBluetooth does when its queue is full.
 */
import { crc32 } from '../../shared/pack.ts';
import { OP, RSP, type PukkTransport } from './protocol.ts';

const PROGRESS_STEP = 4096;

interface Upload {
  name: string;
  size: number;
  crc: number;
  data: Uint8Array;
  received: number;
}

/** Small deterministic PRNG (mulberry32). */
function rng(seed: number): () => number {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export class SimPukk implements PukkTransport {
  packs = new Map<string, Uint8Array>();
  /** Fraction of DATA writes that silently disappear. */
  dropRate = 0;
  /** Flip a bit in one DATA write. */
  corruptOnce = false;
  maxChunk = 240;
  freeBytes = 10 * 1024 * 1024;
  dataWrites = 0;
  resends = 0;
  /** Most data the client ever had in flight beyond the last PROGRESS. */
  maxUnacked = 0;

  private listeners = new Set<(d: Uint8Array) => void>();
  private upload: Upload | null = null;
  private nextProgress = PROGRESS_STEP;
  private resendAt = -1;
  private acked = 0;
  private random = rng(42);

  subscribe(listener: (d: Uint8Array) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private notify(...bytes: number[]): void {
    const msg = Uint8Array.from(bytes);
    setTimeout(() => this.listeners.forEach((l) => l(msg)), 0);
  }

  private notifyU32(rsp: number, v: number): void {
    this.notify(rsp, v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, v >>> 24);
  }

  private error(op: number, code: number): void {
    this.notify(RSP.ERROR, op, code);
  }

  async read(characteristic: 'info' | 'packs'): Promise<Uint8Array> {
    const json = characteristic === 'info'
      ? { fw: 'sim', flash_total: 10 * 1024 * 1024, flash_free: this.freeBytes }
      : [...this.packs].map(([name, d]) => ({ name, title: name, questions: 1, size: d.length }));
    return new TextEncoder().encode(JSON.stringify(json));
  }

  async writeCtrl(d: Uint8Array): Promise<void> {
    const v = new DataView(d.buffer, d.byteOffset, d.byteLength);
    switch (d[0]) {
      case OP.BEGIN: {
        if (this.upload) {
          return this.error(OP.BEGIN, 1);
        }
        const size = v.getUint32(1, true);
        const name = new TextDecoder().decode(d.subarray(10, 10 + d[9]));
        if (d.length !== 10 + d[9] || !/^[a-z0-9_-]{1,32}$/.test(name) || size <= 32) {
          return this.error(OP.BEGIN, 2);
        }
        if (size + 16 * 1024 > this.freeBytes) {
          return this.error(OP.BEGIN, 3);
        }
        this.upload = { name, size, crc: v.getUint32(5, true), data: new Uint8Array(size), received: 0 };
        this.nextProgress = PROGRESS_STEP;
        this.resendAt = -1;
        this.acked = 0;
        return this.notify(RSP.OK, OP.BEGIN, this.maxChunk & 0xff, this.maxChunk >> 8);
      }
      case OP.END: {
        const u = this.upload;
        if (!u) {
          return this.error(OP.END, 4);
        }
        this.upload = null;
        if (u.received !== u.size || crc32(u.data) !== u.crc) {
          return this.error(OP.END, 6);
        }
        this.packs.set(u.name, u.data);
        return this.notify(RSP.OK, OP.END);
      }
      case OP.ABORT:
        this.upload = null;
        return this.notify(RSP.OK, OP.ABORT);
      case OP.DELETE: {
        const name = new TextDecoder().decode(d.subarray(2, 2 + d[1]));
        if (!this.packs.delete(name)) {
          return this.error(OP.DELETE, 9);
        }
        return this.notify(RSP.OK, OP.DELETE);
      }
      default:
        return this.error(d[0], 2);
    }
  }

  async writeData(d: Uint8Array): Promise<void> {
    this.dataWrites++;
    const end = new DataView(d.buffer, d.byteOffset).getUint32(0, true) + d.length - 4;
    this.maxUnacked = Math.max(this.maxUnacked, end - this.acked);
    if (this.random() < this.dropRate) {
      return; // lost on the way
    }
    const u = this.upload;
    if (!u) {
      return this.error(OP.DATA, 4);
    }
    const offset = new DataView(d.buffer, d.byteOffset).getUint32(0, true);
    let payload = d.subarray(4);
    if (offset > u.received) {
      if (this.resendAt !== u.received) {
        this.resendAt = u.received;
        this.resends++;
        this.notifyU32(RSP.RESEND, u.received);
      }
      return;
    }
    payload = payload.subarray(u.received - offset); // skip what we already have
    if (payload.length === 0) {
      return;
    }
    if (u.received + payload.length > u.size) {
      this.upload = null;
      return this.error(OP.DATA, 6);
    }
    u.data.set(payload, u.received);
    if (this.corruptOnce) {
      this.corruptOnce = false;
      u.data[u.received] ^= 0x01;
    }
    u.received += payload.length;
    if (u.received >= this.nextProgress || u.received === u.size) {
      while (this.nextProgress <= u.received) {
        this.nextProgress += PROGRESS_STEP;
      }
      const received = u.received;
      setTimeout(() => (this.acked = received), 0); // the client sees it after delivery
      this.notifyU32(RSP.PROGRESS, received);
    }
  }
}
