import { useState } from 'react';
import { devices, useDevices, type PukkDevice } from '../ble/devices.ts';
import { bluetoothProblem } from '../ble/webbluetooth.ts';
import { formatBytes, initialOf, packColorIndex } from '../theme.ts';

function DeviceCard({ pukk }: { pukk: PukkDevice }) {
  const removePack = (name: string, title: string) => {
    if (confirm(`“${title}” van ${pukk.name} verwijderen?`)) {
      void devices.removePack(pukk.id, name);
    }
  };

  return (
    <div className="card device">
      <div className="device-head">
        <span className={`dot ${pukk.status}`} aria-hidden="true" />
        <strong>{pukk.name}</strong>
        <button className="icon" aria-label={`${pukk.name} vergeten`} onClick={() => devices.forget(pukk.id)}>×</button>
      </div>
      <small className="dim">
        {pukk.status === 'connecting' && 'Verbinden…'}
        {pukk.status === 'disconnected' && 'Niet verbonden'}
        {pukk.status === 'connected' && pukk.info &&
          `Firmware ${pukk.info.fw} · ${formatBytes(pukk.info.flash_free)} vrij`}
      </small>
      {pukk.error && <p className="error small">{pukk.error}</p>}
      {pukk.upload && (
        <div className="upload">
          <small>{pukk.upload.name} versturen…</small>
          <progress value={pukk.upload.sent} max={pukk.upload.total} />
        </div>
      )}
      {pukk.status === 'disconnected' && (
        <button onClick={() => devices.connect(pukk.id)}>Opnieuw verbinden</button>
      )}
      {pukk.status === 'connected' && pukk.packs && (
        <ul className="device-packs">
          {pukk.packs.length === 0 && <li className="dim">Geen packs op deze Pukk</li>}
          {pukk.packs.map((p) => (
            <li key={p.name}>
              <span className={`tile small a${packColorIndex(p.name)}`}>{initialOf(p.title)}</span>
              <span className="device-pack-text">
                {p.title}
                <small className="dim">{p.questions} vragen · {formatBytes(p.size)}</small>
              </span>
              <button className="icon" aria-label={`${p.title} verwijderen`} onClick={() => removePack(p.name, p.title)}>
                ×
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export function DevicePanel() {
  const pukks = useDevices();
  const [error, setError] = useState('');
  const problem = bluetoothProblem();

  const add = async () => {
    setError('');
    try {
      await devices.add();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  return (
    <section className="device-panel">
      <h2>Pukks</h2>
      {problem ? (
        <p className="notice">{problem}</p>
      ) : (
        <button className="primary wide" onClick={add}>Pukk verbinden</button>
      )}
      {error && <p className="error small">{error}</p>}
      {pukks.map((p) => <DeviceCard key={p.id} pukk={p} />)}
    </section>
  );
}
