import { useCallback, useEffect, useState } from 'react';
import { api, ApiError } from './api.ts';
import { DevicePanel } from './components/DevicePanel.tsx';
import { Login } from './components/Login.tsx';
import { PackEditor } from './components/PackEditor.tsx';
import { PackList } from './components/PackList.tsx';
import { parseHash, SessionContext, type Route } from './session.ts';

export function App() {
  const [auth, setAuth] = useState<'checking' | 'in' | 'out'>('checking');
  const [route, setRoute] = useState<Route>(parseHash);

  useEffect(() => {
    api.session()
      .then((s) => setAuth(s.authenticated ? 'in' : 'out'))
      .catch(() => setAuth('out'));
    const onHash = () => setRoute(parseHash());
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  }, []);

  const handleError = useCallback((e: unknown) => {
    if (e instanceof ApiError && e.status === 401) {
      setAuth('out');
    }
    return e instanceof Error ? e.message : String(e);
  }, []);

  const logout = async () => {
    await api.logout().catch(() => undefined);
    setAuth('out');
  };

  if (auth === 'checking') {
    return null;
  }
  if (auth === 'out') {
    return <Login onLogin={() => setAuth('in')} />;
  }

  return (
    <SessionContext.Provider value={{ handleError }}>
      <div className="shell">
        <header className="topbar">
          <a className="wordmark" href="#/">pukk</a>
          <button className="ghost" onClick={logout}>Uitloggen</button>
        </header>
        <div className="layout">
          <main>
            {route.page === 'list'
              ? <PackList />
              : <PackEditor key={route.id} id={route.id} />}
          </main>
          <aside>
            <DevicePanel />
          </aside>
        </div>
      </div>
    </SessionContext.Provider>
  );
}
