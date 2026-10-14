/*
 * Quiz packs: the JSON source format, its rules, and the binary .pukk format
 * the device reads (docs/PACKS.md). Shared by the browser and the server.
 *
 * Mirrors tools/pukk.py; a test checks that both produce identical bytes.
 */

export interface PackQuestion {
  question: string;
  answers: string[];
  correct: number;
  time?: number;
}

export interface PackSource {
  title: string;
  questions: PackQuestion[];
}

export const LIMITS = {
  titleBytes: 40,
  questionBytes: 120,
  answerBytes: 32,
  minAnswers: 2,
  maxAnswers: 4,
  maxQuestions: 1000,
  maxFileSize: 1024 * 1024,
  defaultTime: 15,
  minTime: 5,
  maxTime: 60,
} as const;

/** Where an issue is, so the editor can show it next to the right field. */
export interface IssuePath {
  question?: number;
  field: 'title' | 'questions' | 'question' | 'answers' | 'answer' | 'correct' | 'time' | 'pack';
  answer?: number;
}

export interface Issue {
  path: IssuePath;
  message: string;
}

export interface PackAnalysis {
  errors: Issue[];
  /** Characters that will be replaced when the pack is encoded. */
  warnings: Issue[];
}

export class PackError extends Error {}

// --- character set -----------------------------------------------------------

/* What the device fonts can draw: Latin-1 plus typographic punctuation. Keep
 * in sync with tools/assets/gen_fonts.sh and SUPPORTED_EXTRA in tools/pukk.py. */
const SUPPORTED_EXTRA = new Set('–—‘’“”•…€');

/* Letters outside Latin-1 that Unicode does not decompose into base + accent. */
const FALLBACKS: Record<string, string> = {
  'Ł': 'L', 'ł': 'l', 'Đ': 'D', 'đ': 'd',
  'ı': 'i', 'Œ': 'Oe', 'œ': 'oe',
};

export function isSupportedChar(c: string): boolean {
  const o = c.codePointAt(0)!;
  return c === '\n' || (o >= 0x20 && o <= 0x7e) || (o >= 0xa0 && o <= 0xff) || SUPPORTED_EXTRA.has(c);
}

function stripAccents(c: string): string {
  return c.normalize('NFKD').replace(/\p{M}/gu, '');
}

/** Replace characters the device cannot draw. `unsupported` lists the ones
 * that have no replacement. */
export function normalizeText(text: string): { text: string; changed: boolean; unsupported: string[] } {
  let changed = false;
  const bad = new Set<string>();
  let out = '';
  for (const c of text) {
    if (isSupportedChar(c)) {
      out += c;
      continue;
    }
    const fixed = FALLBACKS[c] ?? stripAccents(c);
    changed = true;
    for (const f of fixed) {
      if (!isSupportedChar(f)) {
        bad.add(f);
      }
    }
    out += fixed;
  }
  return { text: out, changed, unsupported: [...bad].sort() };
}

const encoder = new TextEncoder();

export function utf8Length(text: string): number {
  return encoder.encode(text).length;
}

// --- validation --------------------------------------------------------------

function checkText(
  value: unknown,
  path: IssuePath,
  label: string,
  maxBytes: number,
  out: PackAnalysis,
): string | null {
  if (typeof value !== 'string' || value.trim() === '') {
    out.errors.push({ path, message: `${label} is leeg` });
    return null;
  }
  const n = normalizeText(value.trim());
  if (n.unsupported.length > 0) {
    out.errors.push({ path, message: `${label} bevat tekens die Pukk niet kan tonen: ${n.unsupported.join(' ')}` });
    return null;
  }
  if (n.changed) {
    out.warnings.push({ path, message: `${label} wordt op Pukk: “${n.text}”` });
  }
  const bytes = utf8Length(n.text);
  if (bytes > maxBytes) {
    out.errors.push({ path, message: `${label} is te lang (${bytes} van max. ${maxBytes} bytes)` });
    return null;
  }
  return n.text;
}

interface Normalized {
  title: string;
  questions: { text: string; answers: string[]; correct: number; time: number }[];
}

function analyze(src: unknown): PackAnalysis & { normalized: Normalized | null } {
  const out: PackAnalysis = { errors: [], warnings: [] };
  if (typeof src !== 'object' || src === null) {
    out.errors.push({ path: { field: 'pack' }, message: 'Geen geldig pack' });
    return { ...out, normalized: null };
  }
  const s = src as Partial<PackSource>;
  const title = checkText(s.title, { field: 'title' }, 'Titel', LIMITS.titleBytes, out);

  const questions: Normalized['questions'] = [];
  if (!Array.isArray(s.questions) || s.questions.length === 0) {
    out.errors.push({ path: { field: 'questions' }, message: 'Een pack heeft minstens één vraag nodig' });
  } else if (s.questions.length > LIMITS.maxQuestions) {
    out.errors.push({ path: { field: 'questions' }, message: `Maximaal ${LIMITS.maxQuestions} vragen` });
  } else {
    s.questions.forEach((q, i) => {
      const label = `Vraag ${i + 1}`;
      if (typeof q !== 'object' || q === null) {
        out.errors.push({ path: { question: i, field: 'question' }, message: `${label} is ongeldig` });
        return;
      }
      const text = checkText(q.question, { question: i, field: 'question' }, label, LIMITS.questionBytes, out);
      const answers: string[] = [];
      let answersOk = true;
      if (!Array.isArray(q.answers) || q.answers.length < LIMITS.minAnswers || q.answers.length > LIMITS.maxAnswers) {
        out.errors.push({ path: { question: i, field: 'answers' }, message: `${label}: geef 2 tot 4 antwoorden` });
        answersOk = false;
      } else {
        q.answers.forEach((a, j) => {
          const t = checkText(a, { question: i, field: 'answer', answer: j }, `${label}, antwoord ${j + 1}`,
            LIMITS.answerBytes, out);
          if (t === null) {
            answersOk = false;
          } else {
            answers.push(t);
          }
        });
      }
      const correct = q.correct;
      if (!Number.isInteger(correct) || !Array.isArray(q.answers) || correct < 0 || correct >= q.answers.length) {
        out.errors.push({ path: { question: i, field: 'correct' }, message: `${label}: kies het goede antwoord` });
        answersOk = false;
      }
      const time = q.time ?? LIMITS.defaultTime;
      if (!Number.isInteger(time) || time < LIMITS.minTime || time > LIMITS.maxTime) {
        out.errors.push({ path: { question: i, field: 'time' },
          message: `${label}: tijd moet ${LIMITS.minTime} tot ${LIMITS.maxTime} seconden zijn` });
        answersOk = false;
      }
      if (text !== null && answersOk) {
        questions.push({ text, answers, correct, time });
      }
    });
  }
  const ok = out.errors.length === 0 && title !== null;
  return { ...out, normalized: ok ? { title: title!, questions } : null };
}

/** All problems with a pack, for showing in the editor. */
export function analyzePack(src: unknown): PackAnalysis {
  const { errors, warnings } = analyze(src);
  return { errors, warnings };
}

// --- binary format -----------------------------------------------------------

const MAGIC = [0x50, 0x55, 0x4b, 0x4b]; // "PUKK"
const VERSION = 1;
const HEADER_SIZE = 32;
const QUESTION_SIZE = 24;
const NO_STRING = 0xffffffff;

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    t[i] = c >>> 0;
  }
  return t;
})();

/** CRC-32 (IEEE), same as zlib.crc32. Pass the previous value to continue. */
export function crc32(data: Uint8Array, crc = 0): number {
  let c = ~crc >>> 0;
  for (let i = 0; i < data.length; i++) {
    c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  }
  return ~c >>> 0;
}

class StringTable {
  private parts: Uint8Array[] = [];
  private size = 0;
  private offsets = new Map<string, number>();

  add(text: string): number {
    const known = this.offsets.get(text);
    if (known !== undefined) {
      return known;
    }
    const offset = this.size;
    const bytes = encoder.encode(text);
    this.parts.push(bytes, new Uint8Array([0]));
    this.size += bytes.length + 1;
    this.offsets.set(text, offset);
    return offset;
  }

  bytes(): Uint8Array {
    const out = new Uint8Array(this.size);
    let pos = 0;
    for (const p of this.parts) {
      out.set(p, pos);
      pos += p.length;
    }
    return out;
  }
}

/** Encode a pack to .pukk bytes. Throws PackError with the first problem. */
export function encodePack(src: unknown): { bytes: Uint8Array; warnings: Issue[] } {
  const { errors, warnings, normalized } = analyze(src);
  if (!normalized) {
    throw new PackError(errors[0]?.message ?? 'Geen geldig pack');
  }

  // Same string order as tools/pukk.py: title, then per question its answers
  // and then its text.
  const strings = new StringTable();
  const title = strings.add(normalized.title);
  const index = new Uint8Array(normalized.questions.length * QUESTION_SIZE);
  const iv = new DataView(index.buffer);
  normalized.questions.forEach((q, i) => {
    const offsets = q.answers.map((a) => strings.add(a));
    const base = i * QUESTION_SIZE;
    iv.setUint32(base, strings.add(q.text), true);
    for (let j = 0; j < LIMITS.maxAnswers; j++) {
      iv.setUint32(base + 4 + 4 * j, j < offsets.length ? offsets[j] : NO_STRING, true);
    }
    iv.setUint8(base + 20, q.answers.length);
    iv.setUint8(base + 21, q.correct);
    iv.setUint8(base + 22, q.time === LIMITS.defaultTime ? 0 : q.time);
    iv.setUint8(base + 23, 0);
  });

  const table = strings.bytes();
  const fileSize = HEADER_SIZE + index.length + table.length;
  if (fileSize > LIMITS.maxFileSize) {
    throw new PackError(`Pack is ${fileSize} bytes, maximaal ${LIMITS.maxFileSize}`);
  }

  const out = new Uint8Array(fileSize);
  const h = new DataView(out.buffer);
  out.set(MAGIC, 0);
  h.setUint8(4, VERSION);
  h.setUint8(5, 0);
  h.setUint16(6, normalized.questions.length, true);
  h.setUint32(8, fileSize, true);
  h.setUint32(12, 0, true); // crc32, filled in below
  h.setUint32(16, HEADER_SIZE, true);
  h.setUint32(20, HEADER_SIZE + index.length, true);
  h.setUint32(24, table.length, true);
  h.setUint32(28, title, true);
  out.set(index, HEADER_SIZE);
  out.set(table, HEADER_SIZE + index.length);
  h.setUint32(12, crc32(out), true);
  return { bytes: out, warnings };
}

// --- device pack names -------------------------------------------------------

export const PACK_NAME_RE = /^[a-z0-9_-]{1,32}$/;

/** File name for a pack on the device, derived from its title. */
export function packNameFromTitle(title: string): string {
  const slug = stripAccentsAll(title)
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 32)
    .replace(/-+$/g, '');
  return slug || 'pack';
}

function stripAccentsAll(text: string): string {
  return [...text].map((c) => FALLBACKS[c] ?? stripAccents(c)).join('');
}
