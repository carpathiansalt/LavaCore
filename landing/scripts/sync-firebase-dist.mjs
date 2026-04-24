import { cp, rm } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const source = resolve(root, 'dist');
const target = resolve(root, '..', 'new_app2', 'build', 'landing');

await rm(target, { recursive: true, force: true });
await cp(source, target, { recursive: true });

console.log(`Synced ${source} -> ${target}`);
