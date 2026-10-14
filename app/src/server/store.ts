/*
 * Pack storage in SQLite (node:sqlite, built into Node 24). Packs are kept as
 * their JSON source, drafts included; the device name is unique so two packs
 * never overwrite each other on a Pukk.
 */
import { randomUUID } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { analyzePack, PACK_NAME_RE, packNameFromTitle, type PackSource } from '../shared/pack.ts';

export interface PackSummary {
  id: string;
  name: string;
  title: string;
  questionCount: number;
  valid: boolean;
  updatedAt: string;
}

export interface PackRecord extends PackSummary {
  pack: PackSource;
  createdAt: string;
}

export class NameTakenError extends Error {}

interface Row {
  id: string;
  name: string;
  title: string;
  question_count: number;
  valid: number;
  data: string;
  created_at: string;
  updated_at: string;
}

function toSummary(r: Row): PackSummary {
  return {
    id: r.id,
    name: r.name,
    title: r.title,
    questionCount: r.question_count,
    valid: r.valid === 1,
    updatedAt: r.updated_at,
  };
}

export class PackStore {
  private db: DatabaseSync;

  constructor(path: string) {
    this.db = new DatabaseSync(path);
    this.db.exec(`
      PRAGMA journal_mode = WAL;
      CREATE TABLE IF NOT EXISTS packs (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        title TEXT NOT NULL,
        question_count INTEGER NOT NULL,
        valid INTEGER NOT NULL,
        data TEXT NOT NULL,
        created_at TEXT NOT NULL,
        updated_at TEXT NOT NULL
      );
    `);
  }

  list(): PackSummary[] {
    const rows = this.db.prepare('SELECT * FROM packs ORDER BY title COLLATE NOCASE').all() as unknown as Row[];
    return rows.map(toSummary);
  }

  get(id: string): PackRecord | null {
    const r = this.db.prepare('SELECT * FROM packs WHERE id = ?').get(id) as unknown as Row | undefined;
    if (!r) {
      return null;
    }
    return { ...toSummary(r), pack: JSON.parse(r.data), createdAt: r.created_at };
  }

  /** A free device name based on `title` (adds -2, -3, ... when taken). */
  private freeName(title: string, exceptId?: string): string {
    const base = packNameFromTitle(title);
    for (let n = 1; ; n++) {
      const suffix = n === 1 ? '' : `-${n}`;
      const name = base.slice(0, 32 - suffix.length) + suffix;
      if (!this.nameTaken(name, exceptId)) {
        return name;
      }
    }
  }

  private nameTaken(name: string, exceptId?: string): boolean {
    const r = this.db.prepare('SELECT id FROM packs WHERE name = ?').get(name) as { id: string } | undefined;
    return r !== undefined && r.id !== exceptId;
  }

  create(pack: PackSource, name?: string): PackRecord {
    const id = randomUUID();
    const now = new Date().toISOString();
    const finalName = this.checkName(name, pack.title, id);
    this.db
      .prepare(`INSERT INTO packs (id, name, title, question_count, valid, data, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)`)
      .run(id, finalName, pack.title, pack.questions.length, isValid(pack), JSON.stringify(pack), now, now);
    return this.get(id)!;
  }

  update(id: string, pack: PackSource, name?: string): PackRecord | null {
    const existing = this.get(id);
    if (!existing) {
      return null;
    }
    const finalName = name === undefined ? existing.name : this.checkName(name, pack.title, id);
    this.db
      .prepare(`UPDATE packs SET name = ?, title = ?, question_count = ?, valid = ?, data = ?, updated_at = ?
                WHERE id = ?`)
      .run(finalName, pack.title, pack.questions.length, isValid(pack), JSON.stringify(pack),
        new Date().toISOString(), id);
    return this.get(id);
  }

  delete(id: string): boolean {
    return this.db.prepare('DELETE FROM packs WHERE id = ?').run(id).changes > 0;
  }

  close(): void {
    this.db.close();
  }

  private checkName(name: string | undefined, title: string, id: string): string {
    if (name === undefined) {
      return this.freeName(title, id);
    }
    if (!PACK_NAME_RE.test(name)) {
      throw new NameTakenError('Naam: 1-32 tekens a-z, 0-9, - en _');
    }
    if (this.nameTaken(name, id)) {
      throw new NameTakenError(`Er is al een pack met de naam "${name}"`);
    }
    return name;
  }
}

function isValid(pack: PackSource): number {
  return analyzePack(pack).errors.length === 0 ? 1 : 0;
}
