#!/usr/bin/env node
// Optional developer conversion. The game loads the bundled JSON; Node is not
// required to open, build, host or play the Unreal project.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { stripTypeScriptTypes } from 'node:module';

const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const data = path.join(project, 'Plugins', 'BackyardMMO', 'Content', 'Data');
const source = path.join(data, 'Original', 'MapRoom3');
const dependencies = path.join(project, 'Tools', 'MapRoomGeneration');
const target = path.join(data, 'maproom-world.json');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'bym-original-map-'));
const hashes = {};
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const toSlash = value => value.split(path.sep).join('/');

try {
  fs.writeFileSync(path.join(temporary, 'package.json'), '{"type":"module"}\n');
  const packages = JSON.parse(fs.readFileSync(path.join(dependencies, 'dependencies.json'), 'utf8'));
  for (const [name, expected] of Object.entries(packages.sha256)) {
    const input = path.join(dependencies, 'third_party', name);
    const bytes = fs.readFileSync(input);
    if (hash(bytes) !== expected) throw new Error('Dependency integrity mismatch: ' + name);
    const output = path.join(temporary, 'node_modules', name);
    fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, bytes);
  }
  const compiled = new Set();
  function compile(relative) {
    relative = toSlash(path.normalize(relative));
    if (compiled.has(relative)) return;
    if (relative.startsWith('../') || path.isAbsolute(relative)) throw new Error('Source path escaped reference directory');
    compiled.add(relative);
    const input = path.join(source, relative), bytes = fs.readFileSync(input);
    hashes[relative] = hash(bytes);
    // Execute the original generator and its original dependency graph. Only
    // TypeScript annotations/enums are transformed, never placement logic.
    const javascript = stripTypeScriptTypes(bytes.toString('utf8'), {mode: 'transform', sourceMap: false});
    const output = path.join(temporary, relative.replace(/\.ts$/, '.js'));
    fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, javascript);
    for (const match of javascript.matchAll(/^import\s+[\s\S]*?\sfrom\s*["']([^"']+)["'];/gm)) {
      if (match[1].startsWith('.')) compile(path.join(path.dirname(relative), match[1]).replace(/\.js$/, '.ts'));
      else if (!['alea', 'simplex-noise'].includes(match[1])) throw new Error('Unexpected source dependency: ' + match[1]);
    }
  }
  const base = 'server/src/services/maproom/v3/';
  compile(base + 'generateCells.ts');
  const {getGeneratedCells, cellKey} = await import(pathToFileURL(path.join(temporary, base, 'generateCells.js')).href);
  const {calculateStructureLevel} = await import(pathToFileURL(path.join(temporary, base, 'calculateStructureLevel.js')).href);
  const {getDefenderCoords} = await import(pathToFileURL(path.join(temporary, base, 'getDefenderCoords.js')).href);
  const {getDefenderLevels} = await import(pathToFileURL(path.join(temporary, base, 'getDefenderLevels.js')).href);
  const generated = getGeneratedCells();
  const parents = new Map();
  for (const entry of generated.values()) if (entry.type === 102 || entry.type === 103) {
    const level = calculateStructureLevel(entry.x, entry.y, entry.type);
    const coords = getDefenderCoords(entry.x, entry.y), levels = getDefenderLevels(entry.type, level);
    coords.forEach(([x, y], index) => {
      const key = cellKey(x, y), child = generated.get(key);
      // Original stronghold placement can overlap a neighboring defender. Keep
      // the final source level and only attach a matching parent's metadata.
      if (child?.type === 104 && child.level === levels[index]) parents.set(key, [entry.x, entry.y]);
    });
  }
  const counts = {};
  const cells = [...generated.values()].map(entry => {
    const kind = entry.type ?? 100;
    const level = kind === 102 || kind === 103 ? calculateStructureLevel(entry.x, entry.y, kind) : entry.level ?? 0;
    const parent = parents.get(cellKey(entry.x, entry.y));
    const cell = {x: entry.x, y: entry.y, kind, level,
      terrain: entry.altitude ?? (kind === 100 ? 0 : 5 + (entry.x * 73 + entry.y * 31) % 45)};
    if (kind === 1) cell.tribeIndex = (entry.x + entry.y) % 4;
    if (parent && kind === 104) [cell.parentX, cell.parentY] = parent;
    if (cell.x < 0 || cell.y < 0 || cell.x >= 500 || cell.y >= 500) throw new Error('Original cell outside map');
    counts[kind] = (counts[kind] ?? 0) + 1;
    return cell;
  }).sort((a, b) => a.y - b.y || a.x - b.x);
  if (new Set(cells.map(cell => cellKey(cell.x, cell.y))).size !== cells.length) throw new Error('Duplicate final cell');
  const document = {schema: 1, width: 500, height: 500,
    source: 'Original server getGeneratedCells with alea 1.0.1 and simplex-noise 4.0.3',
    missingCell: {kind: 100, level: 0, terrain: 0}, counts,
    sourceSha256: Object.fromEntries(Object.entries(hashes).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)), cells};
  const bytes = JSON.stringify(document) + '\n';
  if (process.argv.includes('--check')) {
    if (fs.readFileSync(target, 'utf8') !== bytes) throw new Error('Bundled world differs from original source generation');
  } else fs.writeFileSync(target, bytes);
  console.log(JSON.stringify({passed: true, cells: cells.length, counts, bytes: Buffer.byteLength(bytes), sha256: hash(bytes),
    sourceModules: compiled.size, checked: process.argv.includes('--check')}));
} finally {
  fs.rmSync(temporary, {recursive: true, force: true});
}
