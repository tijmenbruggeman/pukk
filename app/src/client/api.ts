/* Thin client for the server API (src/server/app.ts). */
import type { PackSource } from '../shared/pack.ts';
import type { PackRecord, PackSummary } from '../server/store.ts';

export type { PackRecord, PackSummary };

export class ApiError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const res = await fetch(path, {
    method,
    headers: body === undefined ? {} : { 'content-type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (res.status === 204) {
    return undefined as T;
  }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new ApiError(res.status, (data as { error?: string }).error ?? `Fout ${res.status}`);
  }
  return data as T;
}

export const api = {
  session: () => request<{ authenticated: boolean }>('GET', '/api/session'),
  login: (password: string) => request<{ authenticated: boolean }>('POST', '/api/login', { password }),
  logout: () => request<{ authenticated: boolean }>('POST', '/api/logout'),
  listPacks: () => request<PackSummary[]>('GET', '/api/packs'),
  getPack: (id: string) => request<PackRecord>('GET', `/api/packs/${encodeURIComponent(id)}`),
  createPack: (pack: PackSource) => request<PackRecord>('POST', '/api/packs', { pack }),
  updatePack: (id: string, pack: PackSource, name: string) =>
    request<PackRecord>('PUT', `/api/packs/${encodeURIComponent(id)}`, { pack, name }),
  deletePack: (id: string) => request<void>('DELETE', `/api/packs/${encodeURIComponent(id)}`),
};
