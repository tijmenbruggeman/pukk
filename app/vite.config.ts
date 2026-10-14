import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  plugins: [react()],
  root: 'src/client',
  publicDir: false,
  build: {
    outDir: '../../dist/public',
    emptyOutDir: true,
  },
  server: {
    // `npm run dev:server` serves the API on 8787.
    proxy: { '/api': 'http://localhost:8787' },
  },
  test: {
    root: '.',
    include: ['src/**/*.test.ts'],
  },
});
