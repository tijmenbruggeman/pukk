import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  analyzePack, encodePack, LIMITS, PACK_NAME_RE, utf8Length, type PackQuestion, type PackSource,
} from '../../shared/pack.ts';
import { api } from '../api.ts';
import { devices, useDevices } from '../ble/devices.ts';
import { navigate, useSession } from '../session.ts';
import { formatBytes } from '../theme.ts';
import { QuestionCard } from './QuestionCard.tsx';

const NEW_QUESTION: PackQuestion = { question: '', answers: ['', '', '', ''], correct: 0 };

function download(filename: string, text: string): void {
  const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export function PackEditor({ id }: { id: string }) {
  const { handleError } = useSession();
  const [pack, setPack] = useState<PackSource | null>(null);
  const [name, setName] = useState('');
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const pukks = useDevices();

  useEffect(() => {
    api.getPack(id)
      .then((r) => {
        setPack(r.pack);
        setName(r.name);
      })
      .catch((e) => setError(handleError(e)));
  }, [id, handleError]);

  const analysis = useMemo(() => (pack ? analyzePack(pack) : { errors: [], warnings: [] }), [pack]);
  const nameValid = PACK_NAME_RE.test(name);

  const change = (next: PackSource) => {
    setPack(next);
    setDirty(true);
    setNotice('');
  };

  const save = useCallback(async () => {
    if (!pack) {
      return false;
    }
    if (!nameValid) {
      setError('De naam op de Pukk mag alleen a-z, 0-9, - en _ bevatten (max. 32 tekens)');
      return false;
    }
    setSaving(true);
    try {
      const r = await api.updatePack(id, pack, name);
      setName(r.name);
      setDirty(false);
      setError('');
      setNotice('Opgeslagen');
      return true;
    } catch (e) {
      setError(handleError(e));
      return false;
    } finally {
      setSaving(false);
    }
  }, [id, pack, name, nameValid, handleError]);

  // Cmd/Ctrl+S saves; leaving with unsaved changes asks first.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 's') {
        e.preventDefault();
        void save();
      }
    };
    const onLeave = (e: BeforeUnloadEvent) => {
      if (dirty) {
        e.preventDefault();
      }
    };
    window.addEventListener('keydown', onKey);
    window.addEventListener('beforeunload', onLeave);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('beforeunload', onLeave);
    };
  }, [save, dirty]);

  if (!pack) {
    return error ? <p className="error">{error}</p> : null;
  }

  const setQuestion = (i: number, q: PackQuestion) =>
    change({ ...pack, questions: pack.questions.map((x, k) => (k === i ? q : x)) });

  const move = (i: number, delta: -1 | 1) => {
    const qs = [...pack.questions];
    [qs[i], qs[i + delta]] = [qs[i + delta], qs[i]];
    change({ ...pack, questions: qs });
  };

  const back = () => {
    if (!dirty || confirm('Je hebt wijzigingen die nog niet zijn opgeslagen. Toch teruggaan?')) {
      navigate({ page: 'list' });
    }
  };

  const remove = async () => {
    if (!confirm(`“${pack.title}” verwijderen? Packs die al op een Pukk staan blijven daar staan.`)) {
      return;
    }
    try {
      await api.deletePack(id);
      navigate({ page: 'list' });
    } catch (e) {
      setError(handleError(e));
    }
  };

  const push = async (pukkId: string) => {
    if (dirty && !(await save())) {
      return;
    }
    try {
      const { bytes } = encodePack(pack);
      await devices.push(pukkId, name, bytes);
      setNotice(`Op de Pukk gezet (${formatBytes(bytes.length)})`);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const titleIssues = analysis.errors.filter((i) => i.path.field === 'title');
  const connected = pukks.filter((p) => p.status === 'connected');
  const canPush = analysis.errors.length === 0 && nameValid;

  return (
    <section className="editor">
      <div className="page-head">
        <button className="ghost" onClick={back}>← Packs</button>
        <div className="actions">
          <button onClick={() => download(`${name}.json`, JSON.stringify(pack, null, 2) + '\n')}>Exporteren</button>
          <button className="danger" onClick={remove}>Verwijderen</button>
          <button className="primary" disabled={saving || !dirty} onClick={save}>
            {saving ? 'Opslaan…' : dirty ? 'Opslaan' : 'Opgeslagen'}
          </button>
        </div>
      </div>

      <div className="field title-field">
        <input
          className="title-input"
          placeholder="Titel van het pack"
          value={pack.title}
          onChange={(e) => change({ ...pack, title: e.target.value })}
        />
        <span className={`counter${utf8Length(pack.title.trim()) > LIMITS.titleBytes ? ' over' : ''}`}>
          {utf8Length(pack.title.trim())}/{LIMITS.titleBytes}
        </span>
      </div>
      {titleIssues.map((i, k) => <p key={k} className="error small">{i.message}</p>)}
      <label className="name-field">
        Naam op de Pukk
        <input
          value={name}
          spellCheck={false}
          onChange={(e) => {
            setName(e.target.value);
            setDirty(true);
          }}
        />
      </label>

      {error && <p className="error">{error}</p>}
      {notice && <p className="notice">{notice}</p>}

      <div className="push card">
        <div>
          <strong>Naar Pukk sturen</strong>
          <small>
            {analysis.errors.length > 0
              ? `Nog ${analysis.errors.length} ${analysis.errors.length === 1 ? 'punt' : 'punten'} op te lossen`
              : connected.length === 0
                ? 'Verbind eerst een Pukk (rechts)'
                : `${pack.questions.length} ${pack.questions.length === 1 ? 'vraag' : 'vragen'}, klaar om te versturen`}
          </small>
        </div>
        <div className="push-targets">
          {connected.map((p) => (
            <div key={p.id} className="push-target">
              <button className="primary" disabled={!canPush || !!p.upload} onClick={() => push(p.id)}>
                {p.name}
              </button>
              {p.upload && (
                <progress value={p.upload.sent} max={p.upload.total} aria-label={`Upload naar ${p.name}`} />
              )}
            </div>
          ))}
        </div>
      </div>
      {analysis.warnings.length > 0 && (
        <details className="warnings">
          <summary>{analysis.warnings.length} tekst(en) worden op de Pukk iets anders getoond</summary>
          {analysis.warnings.map((w, k) => <p key={k} className="small">{w.message}</p>)}
        </details>
      )}

      {pack.questions.map((q, i) => (
        <QuestionCard
          key={i}
          index={i}
          count={pack.questions.length}
          question={q}
          issues={analysis.errors.filter((e) => e.path.question === i)}
          onChange={(nq) => setQuestion(i, nq)}
          onMove={(d) => move(i, d)}
          onRemove={() => change({ ...pack, questions: pack.questions.filter((_, k) => k !== i) })}
        />
      ))}
      {pack.questions.length < LIMITS.maxQuestions && (
        <button className="add-question" onClick={() => change({ ...pack, questions: [...pack.questions, NEW_QUESTION] })}>
          + Vraag toevoegen
        </button>
      )}
    </section>
  );
}
