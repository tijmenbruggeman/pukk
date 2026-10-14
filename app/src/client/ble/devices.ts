/*
 * Connected Pukks, shared by every part of the UI (and by battle mode later).
 * React components read it with useDevices().
 */
import { useSyncExternalStore } from 'react';
import { deletePack, readInfo, readPacks, uploadPack, type DeviceInfo, type DevicePack } from './protocol.ts';
import { connectPukk, requestPukk, type PukkConnection } from './webbluetooth.ts';

export interface PukkDevice {
  id: string;
  name: string;
  status: 'connecting' | 'connected' | 'disconnected';
  info?: DeviceInfo;
  packs?: DevicePack[];
  upload?: { name: string; sent: number; total: number };
  error?: string;
}

function message(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

class DeviceStore {
  private devices: PukkDevice[] = [];
  private bluetooth = new Map<string, BluetoothDevice>();
  private connections = new Map<string, PukkConnection>();
  private listeners = new Set<() => void>();

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = () => this.devices;

  private update(id: string, patch: Partial<PukkDevice>): void {
    this.devices = this.devices.map((d) => (d.id === id ? { ...d, ...patch } : d));
    this.listeners.forEach((l) => l());
  }

  private conn(id: string): PukkConnection {
    const c = this.connections.get(id);
    if (!c) {
      throw new Error('Pukk is niet verbonden');
    }
    return c;
  }

  /** Ask the browser for a Pukk and connect to it. Resolves quietly when the
   * user closes the chooser. */
  async add(): Promise<void> {
    let device: BluetoothDevice;
    try {
      device = await requestPukk();
    } catch (e) {
      if (e instanceof DOMException && e.name === 'NotFoundError') {
        return; // chooser closed
      }
      throw e;
    }
    if (!this.bluetooth.has(device.id)) {
      this.bluetooth.set(device.id, device);
      device.addEventListener('gattserverdisconnected', () => {
        this.connections.delete(device.id);
        this.update(device.id, { status: 'disconnected', upload: undefined });
      });
      this.devices = [...this.devices, { id: device.id, name: device.name ?? 'Pukk', status: 'connecting' }];
    }
    await this.connect(device.id);
  }

  async connect(id: string): Promise<void> {
    const device = this.bluetooth.get(id);
    if (!device) {
      return;
    }
    this.update(id, { status: 'connecting', error: undefined });
    try {
      this.connections.set(id, await connectPukk(device));
      this.update(id, { status: 'connected' });
      await this.refresh(id);
    } catch (e) {
      this.update(id, { status: 'disconnected', error: message(e) });
    }
  }

  async refresh(id: string): Promise<void> {
    try {
      const c = this.conn(id);
      const info = await readInfo(c);
      const packs = await readPacks(c);
      this.update(id, { info, packs, error: undefined });
    } catch (e) {
      this.update(id, { error: message(e) });
    }
  }

  /** Upload a pack; throws on failure (the device keeps the old version). */
  async push(id: string, name: string, bytes: Uint8Array): Promise<void> {
    this.update(id, { upload: { name, sent: 0, total: bytes.length }, error: undefined });
    try {
      await uploadPack(this.conn(id), name, bytes, {
        onProgress: (sent, total) => this.update(id, { upload: { name, sent, total } }),
      });
    } catch (e) {
      this.update(id, { error: message(e) });
      throw e;
    } finally {
      this.update(id, { upload: undefined });
    }
    await this.refresh(id);
  }

  async removePack(id: string, name: string): Promise<void> {
    try {
      await deletePack(this.conn(id), name);
      await this.refresh(id);
    } catch (e) {
      this.update(id, { error: message(e) });
    }
  }

  forget(id: string): void {
    this.connections.get(id)?.disconnect();
    this.connections.delete(id);
    this.bluetooth.delete(id);
    this.devices = this.devices.filter((d) => d.id !== id);
    this.listeners.forEach((l) => l());
  }
}

export const devices = new DeviceStore();

export function useDevices(): PukkDevice[] {
  return useSyncExternalStore(devices.subscribe, devices.getSnapshot);
}
