import { beforeEach, describe, expect, it } from 'vitest';
import type { PackSource } from '../shared/pack.ts';
import { createApp } from './app.ts';
import { Auth } from './auth.ts';
import { PackStore } from './store.ts';

const PASSWORD = 'geheim';
const pack: PackSource = {
  title: 'Dieren in de Ardèche',
  questions: [{ question: 'Hoeveel poten heeft een spin?', answers: ['6', '8'], correct: 1 }],
};

let app: ReturnType<typeof createApp>;
let cookie = '';

async function call(method: string, path: string, body?: unknown, withCookie = true) {
  const headers: Record<string, string> = {};
  if (body !== undefined) {
    headers['content-type'] = 'application/json';
  }
  if (withCookie && cookie) {
    headers.cookie = cookie;
  }
  return app.request(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
}

beforeEach(async () => {
  app = createApp({ store: new PackStore(':memory:'), auth: new Auth(PASSWORD, 'test-secret') });
  cookie = '';
  const res = await call('POST', '/api/login', { password: PASSWORD });
  cookie = res.headers.get('set-cookie')!.split(';')[0];
});

describe('auth', () => {
  it('rejects a wrong password', async () => {
    const res = await call('POST', '/api/login', { password: 'nee' }, false);
    expect(res.status).toBe(401);
  });

  it('sets a secure, http-only session cookie', async () => {
    const res = await call('POST', '/api/login', { password: PASSWORD }, false);
    const setCookie = res.headers.get('set-cookie')!;
    expect(setCookie).toMatch(/HttpOnly/);
    expect(setCookie).toMatch(/Secure/);
    expect(setCookie).toMatch(/SameSite=Strict/);
  });

  it('protects the pack API', async () => {
    expect((await call('GET', '/api/packs', undefined, false)).status).toBe(401);
    expect((await call('GET', '/api/packs/x', undefined, false)).status).toBe(401);
    expect((await call('POST', '/api/packs', { pack }, false)).status).toBe(401);
    expect((await call('GET', '/api/packs')).status).toBe(200);
  });

  it('reports the session state', async () => {
    expect(await (await call('GET', '/api/session')).json()).toEqual({ authenticated: true });
    expect(await (await call('GET', '/api/session', undefined, false)).json()).toEqual({ authenticated: false });
  });

  it('rejects a tampered or expired session', () => {
    const auth = new Auth(PASSWORD, 's');
    const token = auth.createSession();
    expect(auth.verifySession(token)).toBe(true);
    expect(auth.verifySession(token.replace(/.$/, 'x'))).toBe(false);
    expect(auth.verifySession(token, Date.now() + 31 * 24 * 3600 * 1000)).toBe(false);
    expect(new Auth('ander wachtwoord', 's').verifySession(token)).toBe(false);
  });

  it('slows down password guessing', async () => {
    for (let i = 0; i < 10; i++) {
      await call('POST', '/api/login', { password: `fout${i}` }, false);
    }
    const res = await call('POST', '/api/login', { password: PASSWORD }, false);
    expect(res.status).toBe(429);
  });
});

describe('packs', () => {
  it('creates, reads, updates and deletes a pack', async () => {
    const created = await (await call('POST', '/api/packs', { pack })).json();
    expect(created).toMatchObject({ name: 'dieren-in-de-ardeche', title: pack.title, questionCount: 1, valid: true });

    const got = await (await call('GET', `/api/packs/${created.id}`)).json();
    expect(got.pack).toEqual(pack);

    const changed = { ...pack, questions: [...pack.questions, pack.questions[0]] };
    const updated = await (await call('PUT', `/api/packs/${created.id}`, { pack: changed })).json();
    expect(updated.questionCount).toBe(2);
    expect(updated.name).toBe('dieren-in-de-ardeche'); // name stays unless given

    const list = await (await call('GET', '/api/packs')).json();
    expect(list).toHaveLength(1);

    expect((await call('DELETE', `/api/packs/${created.id}`)).status).toBe(204);
    expect((await call('GET', `/api/packs/${created.id}`)).status).toBe(404);
  });

  it('keeps questions per game and order', async () => {
    const withOptions = { ...pack, questions_per_game: 20, order: 'random' };
    const created = await (await call('POST', '/api/packs', { pack: withOptions })).json();
    expect(created.valid).toBe(true);
    const got = await (await call('GET', `/api/packs/${created.id}`)).json();
    expect(got.pack).toEqual(withOptions);
    expect((await call('POST', '/api/packs', { pack: { ...pack, order: 'shuffle' } })).status).toBe(400);
    expect((await call('POST', '/api/packs', { pack: { ...pack, questions_per_game: 'veel' } })).status).toBe(400);
  });

  it('stores unfinished drafts but marks them invalid', async () => {
    const draft = { title: 'Nog niks', questions: [{ question: '', answers: ['', ''], correct: 0 }] };
    const res = await call('POST', '/api/packs', { pack: draft });
    expect(res.status).toBe(201);
    expect((await res.json()).valid).toBe(false);
  });

  it('gives each pack a unique device name', async () => {
    const a = await (await call('POST', '/api/packs', { pack })).json();
    const b = await (await call('POST', '/api/packs', { pack })).json();
    expect(a.name).toBe('dieren-in-de-ardeche');
    expect(b.name).toBe('dieren-in-de-ardeche-2');
    const clash = await call('PUT', `/api/packs/${b.id}`, { pack, name: a.name });
    expect(clash.status).toBe(409);
    const bad = await call('PUT', `/api/packs/${b.id}`, { pack, name: 'Niet Geldig' });
    expect(bad.status).toBe(409);
  });

  it('rejects malformed input', async () => {
    for (const body of [null, {}, { pack: { title: 1, questions: [] } }, { pack: { title: 'x', questions: [1] } },
      { pack: { title: 'x', questions: [{ question: 'q', answers: 'a', correct: 0 }] } }]) {
      expect((await call('POST', '/api/packs', body)).status).toBe(400);
    }
  });
});
