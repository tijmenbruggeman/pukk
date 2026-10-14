import { useEffect, useRef, useState, type ChangeEvent } from 'react';
import { api, type PackSummary } from '../api.ts';
import { navigate, useSession } from '../session.ts';
import { initialOf, packColorIndex } from '../theme.ts';

const EMPTY_PACK = {
  title: 'Nieuw pack',
  questions: [{ question: '', answers: ['', '', '', ''], correct: 0 }],
};

export function PackList() {
  const { handleError } = useSession();
  const [packs, setPacks] = useState<PackSummary[] | null>(null);
  const [error, setError] = useState('');
  const fileInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    api.listPacks().then(setPacks).catch((e) => setError(handleError(e)));
  }, [handleError]);

  const create = async () => {
    try {
      const pack = await api.createPack(EMPTY_PACK);
      navigate({ page: 'pack', id: pack.id });
    } catch (e) {
      setError(handleError(e));
    }
  };

  const importFile = async (e: ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) {
      return;
    }
    try {
      const pack = await api.createPack(JSON.parse(await file.text()));
      navigate({ page: 'pack', id: pack.id });
    } catch (err) {
      setError(err instanceof SyntaxError ? `${file.name} is geen geldig JSON-bestand` : handleError(err));
    }
  };

  return (
    <section>
      <div className="page-head">
        <h1>Jouw packs</h1>
        <div className="actions">
          <button onClick={() => fileInput.current?.click()}>Importeren</button>
          <button className="primary" onClick={create}>Nieuw pack</button>
          <input ref={fileInput} type="file" accept=".json,application/json" hidden onChange={importFile} />
        </div>
      </div>
      {error && <p className="error">{error}</p>}
      {packs?.length === 0 && (
        <div className="empty card">
          <h2>Nog geen packs</h2>
          <p>Maak een nieuw pack of importeer een JSON-bestand (zoals <code>packs/voorbeeld.json</code>).</p>
        </div>
      )}
      <div className="pack-grid">
        {packs?.map((p) => (
          <a key={p.id} className="card pack-card" href={`#/pack/${p.id}`}>
            <span className={`tile a${packColorIndex(p.name)}`}>{initialOf(p.title)}</span>
            <span className="pack-card-text">
              <strong>{p.title || 'Zonder titel'}</strong>
              <small>
                {p.questionCount} {p.questionCount === 1 ? 'vraag' : 'vragen'}
                {!p.valid && <span className="badge warn">concept</span>}
              </small>
            </span>
          </a>
        ))}
      </div>
    </section>
  );
}
