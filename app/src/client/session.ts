/* Routing (hash based) and the session context shared by all pages. */
import { createContext, useContext } from 'react';

export type Route = { page: 'list' } | { page: 'pack'; id: string };

export function parseHash(): Route {
  const m = location.hash.match(/^#\/pack\/([\w-]+)$/);
  return m ? { page: 'pack', id: m[1] } : { page: 'list' };
}

export function navigate(route: Route): void {
  location.hash = route.page === 'pack' ? `#/pack/${route.id}` : '#/';
}

export interface Session {
  /** Turn an error into a message; a lost session sends the user to login. */
  handleError: (e: unknown) => string;
}

export const SessionContext = createContext<Session>({ handleError: (e) => String(e) });

export function useSession(): Session {
  return useContext(SessionContext);
}
