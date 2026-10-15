/*
 * Server entry point. Configuration comes from the environment:
 *
 *   PUKK_PASSWORD    required; the one password for the app
 *   PUKK_DATA_DIR    where the SQLite database lives (default ./data)
 *   PUKK_SECRET      optional; signs session cookies (default: a random
 *                    secret kept in the data directory)
 *   PORT             default 8080
 *   HOST             address to listen on (default: all interfaces)
 */
import { serve } from '@hono/node-server';
import { randomBytes } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createApp } from './app.ts';
import { Auth } from './auth.ts';
import { PackStore } from './store.ts';

const password = process.env.PUKK_PASSWORD;
if (!password) {
  console.error('PUKK_PASSWORD is not set');
  process.exit(1);
}

const dataDir = process.env.PUKK_DATA_DIR ?? './data';
mkdirSync(dataDir, { recursive: true });

function sessionSecret(): string {
  if (process.env.PUKK_SECRET) {
    return process.env.PUKK_SECRET;
  }
  const file = join(dataDir, 'session-secret');
  if (!existsSync(file)) {
    writeFileSync(file, randomBytes(32).toString('base64url'), { mode: 0o600 });
  }
  return readFileSync(file, 'utf8').trim();
}

// The built client sits next to the bundled server (dist/public).
const staticDir = fileURLToPath(new URL('./public', import.meta.url));
const store = new PackStore(join(dataDir, 'pukk.db'));
const app = createApp({
  store,
  auth: new Auth(password, sessionSecret()),
  staticDir: existsSync(staticDir) ? staticDir : undefined,
});

const port = Number(process.env.PORT ?? 8080);
const hostname = process.env.HOST || undefined;
const server = serve({ fetch: app.fetch, port, hostname }, () => {
  console.log(`pukk app listening on ${hostname ?? ''}:${port} (data in ${dataDir})`);
});

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.on(signal, () => {
    server.close();
    store.close();
    process.exit(0);
  });
}
