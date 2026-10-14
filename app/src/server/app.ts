/*
 * HTTP API and static file serving.
 *
 *   GET    /api/session       {authenticated}
 *   POST   /api/login         {password}
 *   POST   /api/logout
 *   GET    /api/packs         [PackSummary]
 *   POST   /api/packs         {pack, name?}  -> PackRecord
 *   GET    /api/packs/:id     PackRecord
 *   PUT    /api/packs/:id     {pack, name?}  -> PackRecord
 *   DELETE /api/packs/:id
 *   GET    /healthz
 */
import { serveStatic } from '@hono/node-server/serve-static';
import { Hono, type Context } from 'hono';
import { bodyLimit } from 'hono/body-limit';
import { deleteCookie, getCookie, setCookie } from 'hono/cookie';
import { secureHeaders } from 'hono/secure-headers';
import { LIMITS, type PackSource } from '../shared/pack.ts';
import { Auth, SESSION_COOKIE, SESSION_DAYS } from './auth.ts';
import { NameTakenError, type PackStore } from './store.ts';

export interface AppOptions {
  store: PackStore;
  auth: Auth;
  /** Built client (dist/public); omitted in development and tests. */
  staticDir?: string;
}

/* Generous bounds for drafts: the editor shows the real rules; this only keeps
 * garbage out. */
const DRAFT = { text: 1000, answers: 6, questions: LIMITS.maxQuestions };

function isString(v: unknown, max: number): v is string {
  return typeof v === 'string' && v.length <= max;
}

/** A well-formed (possibly unfinished) pack, or an error message. */
function parseDraft(body: unknown): { pack: PackSource; name?: string } | string {
  if (typeof body !== 'object' || body === null) {
    return 'Ongeldig verzoek';
  }
  const { pack, name } = body as { pack?: unknown; name?: unknown };
  if (name !== undefined && typeof name !== 'string') {
    return 'Ongeldige naam';
  }
  if (typeof pack !== 'object' || pack === null) {
    return 'Pack ontbreekt';
  }
  const p = pack as Record<string, unknown>;
  if (!isString(p.title, DRAFT.text) || !Array.isArray(p.questions) || p.questions.length > DRAFT.questions) {
    return 'Ongeldig pack';
  }
  const questions = [];
  for (const q of p.questions as unknown[]) {
    if (typeof q !== 'object' || q === null) {
      return 'Ongeldige vraag';
    }
    const { question, answers, correct, time } = q as Record<string, unknown>;
    if (!isString(question, DRAFT.text) || !Array.isArray(answers) || answers.length > DRAFT.answers ||
        !answers.every((a) => isString(a, DRAFT.text)) || !Number.isInteger(correct) ||
        (time !== undefined && !Number.isInteger(time))) {
      return 'Ongeldige vraag';
    }
    questions.push({ question, answers, correct: correct as number, ...(time === undefined ? {} : { time: time as number }) });
  }
  return { pack: { title: p.title, questions }, name };
}

export function createApp({ store, auth, staticDir }: AppOptions): Hono {
  const app = new Hono();
  app.use(secureHeaders());

  app.get('/healthz', (c) => c.text('ok'));

  const api = new Hono();
  api.use(bodyLimit({ maxSize: 2 * 1024 * 1024 }));

  const authenticated = (c: Context) => auth.verifySession(getCookie(c, SESSION_COOKIE));

  api.get('/session', (c) => c.json({ authenticated: authenticated(c) }));

  api.post('/login', async (c) => {
    if (auth.isRateLimited()) {
      return c.json({ error: 'Te veel pogingen, wacht een minuut' }, 429);
    }
    const body = await c.req.json().catch(() => ({}));
    const password = (body as { password?: unknown }).password;
    if (typeof password !== 'string' || !auth.checkPassword(password)) {
      auth.recordFailure();
      return c.json({ error: 'Wachtwoord klopt niet' }, 401);
    }
    setCookie(c, SESSION_COOKIE, auth.createSession(), {
      httpOnly: true,
      secure: true,
      sameSite: 'Strict',
      path: '/',
      maxAge: SESSION_DAYS * 24 * 3600,
    });
    return c.json({ authenticated: true });
  });

  api.post('/logout', (c) => {
    deleteCookie(c, SESSION_COOKIE, { path: '/', secure: true });
    return c.json({ authenticated: false });
  });

  // Everything below needs a session.
  api.use('/packs/*', async (c, next) => {
    if (!authenticated(c)) {
      return c.json({ error: 'Niet ingelogd' }, 401);
    }
    await next();
  });
  api.use('/packs', async (c, next) => {
    if (!authenticated(c)) {
      return c.json({ error: 'Niet ingelogd' }, 401);
    }
    await next();
  });

  api.get('/packs', (c) => c.json(store.list()));

  api.post('/packs', async (c) => {
    const draft = parseDraft(await c.req.json().catch(() => null));
    if (typeof draft === 'string') {
      return c.json({ error: draft }, 400);
    }
    try {
      return c.json(store.create(draft.pack, draft.name), 201);
    } catch (e) {
      if (e instanceof NameTakenError) {
        return c.json({ error: e.message }, 409);
      }
      throw e;
    }
  });

  api.get('/packs/:id', (c) => {
    const pack = store.get(c.req.param('id'));
    return pack ? c.json(pack) : c.json({ error: 'Pack niet gevonden' }, 404);
  });

  api.put('/packs/:id', async (c) => {
    const draft = parseDraft(await c.req.json().catch(() => null));
    if (typeof draft === 'string') {
      return c.json({ error: draft }, 400);
    }
    try {
      const pack = store.update(c.req.param('id'), draft.pack, draft.name);
      return pack ? c.json(pack) : c.json({ error: 'Pack niet gevonden' }, 404);
    } catch (e) {
      if (e instanceof NameTakenError) {
        return c.json({ error: e.message }, 409);
      }
      throw e;
    }
  });

  api.delete('/packs/:id', (c) =>
    store.delete(c.req.param('id')) ? c.body(null, 204) : c.json({ error: 'Pack niet gevonden' }, 404));

  app.route('/api', api);

  if (staticDir) {
    app.use('/*', serveStatic({ root: staticDir }));
    // Single-page app: any other path gets index.html.
    app.get('*', serveStatic({ path: `${staticDir}/index.html` }));
  }
  return app;
}
