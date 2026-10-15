import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { analyzePack, crc32, encodePack, packNameFromTitle, PackError, type PackSource } from './pack.ts';

const ROOT = resolve(import.meta.dirname, '../../..');

/** Build a pack with tools/pukk.py, the reference implementation. */
function buildWithPython(src: unknown): Uint8Array {
  const dir = mkdtempSync(join(tmpdir(), 'pukk-'));
  const json = join(dir, 'pack.json');
  const out = join(dir, 'pack.pukk');
  writeFileSync(json, JSON.stringify(src));
  execFileSync('python3', [join(ROOT, 'tools/pukk.py'), 'build', json, '-o', out], { stdio: 'pipe' });
  return new Uint8Array(readFileSync(out));
}

function samplePack(): PackSource {
  return JSON.parse(readFileSync(join(ROOT, 'packs/voorbeeld.json'), 'utf8'));
}

const valid: PackSource = {
  title: 'Test',
  questions: [{ question: 'Twee plus twee?', answers: ['3', '4'], correct: 1 }],
};

describe('encodePack', () => {
  it('produces the same bytes as tools/pukk.py for the sample pack', () => {
    const src = samplePack();
    expect(encodePack(src).bytes).toEqual(buildWithPython(src));
  });

  it('produces the same bytes as tools/pukk.py for a large pack with replacements', () => {
    const questions = Array.from({ length: 1000 }, (_, i) => ({
      question: `Vraag ${i + 1}: hoeveel is ${i} × 7? Łódź ‘café’ €`,
      answers: [String(i * 7), String(i * 7 + 1), String(i * 7 - 1), 'geen idee'].slice(0, 2 + (i % 3)),
      correct: 0,
      time: 5 + (i % 56),
    }));
    const src = { title: 'Groot pack – Ç’est ça', questions };
    const ts = encodePack(src);
    expect(ts.bytes).toEqual(buildWithPython(src));
    expect(ts.warnings.length).toBeGreaterThan(0); // Ł, ź and × are replaced
  });

  it('produces the same bytes as tools/pukk.py with questions per game and random order', () => {
    const src = { ...samplePack(), questions_per_game: 5, order: 'random' as const };
    const bytes = encodePack(src).bytes;
    expect(bytes).toEqual(buildWithPython(src));
    expect(bytes[5]).toBe(1);
    expect(new DataView(bytes.buffer).getUint16(32, true)).toBe(5);
  });

  it('throws on an invalid pack', () => {
    expect(() => encodePack({ title: '', questions: [] })).toThrow(PackError);
  });
});

describe('crc32', () => {
  it('matches the standard check value', () => {
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
  });

  it('continues from a previous value', () => {
    const a = new TextEncoder().encode('1234');
    const b = new TextEncoder().encode('56789');
    expect(crc32(b, crc32(a))).toBe(0xcbf43926);
  });
});

describe('analyzePack', () => {
  it('accepts a valid pack', () => {
    expect(analyzePack(valid)).toEqual({ errors: [], warnings: [] });
  });

  it('reports each problem with its location', () => {
    const { errors } = analyzePack({
      title: 'x'.repeat(41),
      questions: [
        { question: '', answers: ['a', 'b'], correct: 0 },
        { question: 'ok', answers: ['a'], correct: 0 },
        { question: 'ok', answers: ['a', 'b'], correct: 2 },
        { question: 'ok', answers: ['a', 'b'], correct: 0, time: 61 },
        { question: 'é'.repeat(61), answers: ['a', 'b'], correct: 0 },
        { question: 'ok 🎉', answers: ['a', 'b'], correct: 0 },
      ],
    });
    expect(errors.map((e) => [e.path.field, e.path.question])).toEqual([
      ['title', undefined],
      ['question', 0],
      ['answers', 1],
      ['correct', 2],
      ['time', 3],
      ['question', 4],
      ['question', 5],
    ]);
    expect(errors[5].message).toContain('122 van max. 120 bytes');
    expect(errors[6].message).toContain('🎉');
  });

  it('warns about characters that will be replaced', () => {
    const { errors, warnings } = analyzePack({ ...valid, title: 'Łódź' });
    expect(errors).toEqual([]);
    expect(warnings[0].message).toContain('Lódz');
  });

  it('keeps Latin-1 and typographic punctuation as is', () => {
    expect(analyzePack({ ...valid, title: 'Ardèche – “ja” €' }).warnings).toEqual([]);
  });

  it('checks questions per game and order', () => {
    expect(analyzePack({ ...valid, questions_per_game: 20, order: 'random' }).errors).toEqual([]);
    const { errors } = analyzePack({ ...valid, questions_per_game: 0, order: 'shuffle' as never });
    expect(errors.map((e) => e.path.field)).toEqual(['questions_per_game', 'order']);
    expect(analyzePack({ ...valid, questions_per_game: 1001 }).errors).toHaveLength(1);
  });

  it('rejects something that is not a pack', () => {
    expect(analyzePack(null).errors).toHaveLength(1);
    expect(analyzePack({ title: 'x', questions: 'nope' }).errors[0].path.field).toBe('questions');
  });
});

describe('packNameFromTitle', () => {
  it('makes a device-safe name', () => {
    expect(packNameFromTitle('Dieren in de Ardèche')).toBe('dieren-in-de-ardeche');
    expect(packNameFromTitle('  Sport & spel!! ')).toBe('sport-spel');
    expect(packNameFromTitle('Łódź')).toBe('lodz');
    expect(packNameFromTitle('🎉')).toBe('pack');
    expect(packNameFromTitle('a'.repeat(40))).toHaveLength(32);
  });
});
