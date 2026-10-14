/* The device's colors, so packs look the same in the app and on a Pukk
 * (see components/ui/ui_theme.c). */

export const ANSWER_SHAPES = ['triangle', 'diamond', 'circle', 'square'] as const;

/** Same hash as ui_pack_color() on the device: FNV-1a over the pack name. */
export function packColorIndex(name: string): number {
  let h = 2166136261;
  for (const b of new TextEncoder().encode(name)) {
    h = Math.imul(h ^ b, 16777619) >>> 0;
  }
  return h % 4;
}

/** First character of a title, upper-cased (as on the device). */
export function initialOf(title: string): string {
  const first = [...title.trim()][0] ?? '?';
  return /[a-z]/.test(first) ? first.toUpperCase() : first;
}

export function formatBytes(n: number): string {
  if (n >= 1024 * 1024) {
    return `${(n / 1024 / 1024).toFixed(1).replace('.', ',')} MB`;
  }
  return n >= 1024 ? `${Math.round(n / 1024)} KB` : `${n} bytes`;
}
