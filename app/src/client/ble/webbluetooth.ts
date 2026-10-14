/*
 * Web Bluetooth adapter for the protocol in protocol.ts. Works in Chrome and
 * Edge (desktop and Android) on https or localhost; not in Safari or Firefox.
 */
import {
  CTRL_UUID, DATA_UUID, INFO_UUID, PACKS_UUID, SERVICE_UUID, type PukkTransport,
} from './protocol.ts';

/** Why Bluetooth cannot be used here, or null when it can. */
export function bluetoothProblem(): string | null {
  if (!window.isSecureContext) {
    return 'Bluetooth werkt alleen als de app via https geopend is.';
  }
  if (!navigator.bluetooth) {
    return 'Deze browser kan geen Bluetooth. Gebruik Chrome of Edge op een computer of Android-telefoon.';
  }
  return null;
}

/** Let the user pick a Pukk in the browser's device chooser. */
export function requestPukk(): Promise<BluetoothDevice> {
  return navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE_UUID] }, { namePrefix: 'Pukk-' }],
    optionalServices: [SERVICE_UUID],
  });
}

export interface PukkConnection extends PukkTransport {
  disconnect(): void;
}

function copyBytes(v: DataView): Uint8Array {
  return new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength));
}

export async function connectPukk(device: BluetoothDevice): Promise<PukkConnection> {
  const server = await device.gatt!.connect();
  const service = await server.getPrimaryService(SERVICE_UUID);
  // One at a time: Chrome rejects GATT operations that overlap.
  const ctrl = await service.getCharacteristic(CTRL_UUID);
  const data = await service.getCharacteristic(DATA_UUID);
  const info = await service.getCharacteristic(INFO_UUID);
  const packs = await service.getCharacteristic(PACKS_UUID);

  const listeners = new Set<(d: Uint8Array) => void>();
  ctrl.addEventListener('characteristicvaluechanged', (e) => {
    const bytes = copyBytes((e.target as BluetoothRemoteGATTCharacteristic).value!);
    listeners.forEach((l) => l(bytes));
  });
  await ctrl.startNotifications();

  let queue: Promise<unknown> = Promise.resolve();
  const serial = <T>(op: () => Promise<T>): Promise<T> => {
    const p = queue.then(op, op);
    queue = p.catch(() => undefined);
    return p;
  };

  return {
    writeCtrl: (d) => serial(() => ctrl.writeValueWithResponse(d as Uint8Array<ArrayBuffer>)),
    writeData: (d) => serial(() => data.writeValueWithoutResponse(d as Uint8Array<ArrayBuffer>)),
    read: async (which) => copyBytes(await serial(() => (which === 'info' ? info : packs).readValue())),
    subscribe: (l) => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    disconnect: () => device.gatt?.disconnect(),
  };
}
