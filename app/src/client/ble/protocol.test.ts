import { describe, expect, it } from 'vitest';
import { deletePack, DeviceError, readInfo, readPacks, uploadPack } from './protocol.ts';
import { SimPukk } from './sim.ts';

function data(size: number): Uint8Array {
  const d = new Uint8Array(size);
  for (let i = 0; i < size; i++) {
    d[i] = (i * 31 + 7) & 0xff;
  }
  return d;
}

const fast = { stallMs: 20 };

describe('uploadPack', () => {
  it('uploads a small pack', async () => {
    const pukk = new SimPukk();
    await uploadPack(pukk, 'klein', data(667), fast);
    expect(pukk.packs.get('klein')).toEqual(data(667));
  });

  it('uploads a large pack and reports progress', async () => {
    const pukk = new SimPukk();
    const seen: number[] = [];
    await uploadPack(pukk, 'groot', data(71_000), { ...fast, onProgress: (sent) => seen.push(sent) });
    expect(pukk.packs.get('groot')).toEqual(data(71_000));
    expect(seen.at(-1)).toBe(71_000);
    expect(seen).toEqual([...seen].sort((a, b) => a - b)); // never goes back
  });

  it('recovers when writes get lost', async () => {
    const pukk = new SimPukk();
    pukk.dropRate = 0.1;
    await uploadPack(pukk, 'verlies', data(40_000), fast);
    expect(pukk.packs.get('verlies')).toEqual(data(40_000));
    expect(pukk.resends).toBeGreaterThan(0);
  });

  it('recovers when the last writes get lost (no gap to report)', async () => {
    const pukk = new SimPukk();
    const total = 5000;
    const lastPacket = Math.ceil(total / pukk.maxChunk) - 1;
    let n = 0;
    const write = pukk.writeData.bind(pukk);
    pukk.writeData = async (d) => (n++ === lastPacket ? undefined : write(d));
    await uploadPack(pukk, 'staart', data(total), fast);
    expect(pukk.packs.get('staart')).toEqual(data(total));
  });

  it('keeps unacknowledged data within the window', async () => {
    const pukk = new SimPukk();
    await uploadPack(pukk, 'venster', data(30_000), { ...fast, window: 4096 });
    expect(pukk.maxUnacked).toBeLessThanOrEqual(4096 + pukk.maxChunk);
    expect(pukk.dataWrites).toBe(Math.ceil(30_000 / pukk.maxChunk)); // nothing sent twice
  });

  it('fails cleanly when data arrives damaged', async () => {
    const pukk = new SimPukk();
    pukk.corruptOnce = true;
    await expect(uploadPack(pukk, 'kapot', data(3000), fast)).rejects.toMatchObject({ code: 6 });
    expect(pukk.packs.has('kapot')).toBe(false);
  });

  it('reports a full device', async () => {
    const pukk = new SimPukk();
    pukk.freeBytes = 1000;
    const err = await uploadPack(pukk, 'vol', data(3000), fast).catch((e) => e);
    expect(err).toBeInstanceOf(DeviceError);
    expect(err.message).toContain('ruimte');
  });

  it('gives up when the device stops answering', async () => {
    const pukk = new SimPukk();
    const write = pukk.writeData.bind(pukk);
    let n = 0;
    pukk.writeData = async (d) => (n++ < 3 ? write(d) : undefined);
    await expect(uploadPack(pukk, 'stil', data(5000), { stallMs: 5, maxStalls: 2 })).rejects.toThrow('vast');
  });
});

describe('device info and packs', () => {
  it('reads info, lists and deletes packs', async () => {
    const pukk = new SimPukk();
    await uploadPack(pukk, 'een', data(100), fast);
    expect((await readInfo(pukk)).fw).toBe('sim');
    expect((await readPacks(pukk)).map((p) => p.name)).toEqual(['een']);
    await deletePack(pukk, 'een');
    expect(await readPacks(pukk)).toEqual([]);
    await expect(deletePack(pukk, 'een')).rejects.toMatchObject({ code: 9 });
  });
});
