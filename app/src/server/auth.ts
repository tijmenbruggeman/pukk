/*
 * One shared password, stateless session cookies. A session is
 * "<expiry>.<hmac>", signed with a secret and the password hash, so changing
 * the password logs everyone out.
 */
import { createHash, createHmac, timingSafeEqual } from 'node:crypto';

export const SESSION_COOKIE = 'pukk_session';
export const SESSION_DAYS = 30;

const LOGIN_WINDOW_MS = 60_000;
const LOGIN_MAX_FAILURES = 10;

function sha256(text: string): Buffer {
  return createHash('sha256').update(text).digest();
}

export class Auth {
  private passwordHash: Buffer;
  private secret: string;
  private failures: number[] = [];

  constructor(password: string, secret: string) {
    this.passwordHash = sha256(password);
    this.secret = secret;
  }

  checkPassword(attempt: string): boolean {
    return timingSafeEqual(sha256(attempt), this.passwordHash);
  }

  /** Too many wrong passwords in the last minute (from anyone)? */
  isRateLimited(now = Date.now()): boolean {
    this.failures = this.failures.filter((t) => now - t < LOGIN_WINDOW_MS);
    return this.failures.length >= LOGIN_MAX_FAILURES;
  }

  recordFailure(now = Date.now()): void {
    this.failures.push(now);
  }

  private sign(expires: number): string {
    return createHmac('sha256', this.secret)
      .update(`${expires}.`)
      .update(this.passwordHash)
      .digest('base64url');
  }

  createSession(now = Date.now()): string {
    const expires = now + SESSION_DAYS * 24 * 3600 * 1000;
    return `${expires}.${this.sign(expires)}`;
  }

  verifySession(token: string | undefined, now = Date.now()): boolean {
    if (!token) {
      return false;
    }
    const [expiresText, mac] = token.split('.');
    const expires = Number(expiresText);
    if (!Number.isSafeInteger(expires) || expires < now || !mac) {
      return false;
    }
    const expected = Buffer.from(this.sign(expires));
    const given = Buffer.from(mac);
    return given.length === expected.length && timingSafeEqual(given, expected);
  }
}
