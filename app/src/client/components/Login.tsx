import { useState, type FormEvent } from 'react';
import { api } from '../api.ts';

export function Login({ onLogin }: { onLogin: () => void }) {
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (!password) {
      setError('Vul het wachtwoord in');
      return;
    }
    setBusy(true);
    try {
      await api.login(password);
      onLogin();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setBusy(false);
    }
  };

  return (
    <div className="login">
      <form className="card login-card" onSubmit={submit}>
        <div className="wordmark big">pukk</div>
        <label htmlFor="password">Wachtwoord</label>
        <input
          id="password"
          type="password"
          autoComplete="current-password"
          autoFocus
          value={password}
          onChange={(e) => {
            setPassword(e.target.value);
            setError('');
          }}
        />
        {error && <p className="error">{error}</p>}
        <button className="primary" disabled={busy}>Inloggen</button>
      </form>
    </div>
  );
}
