"""Original deterministic Map Room 3 world data and generation regression."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import unittest

PROJECT = Path(__file__).resolve().parents[1]
DATA = PROJECT / 'Plugins/BackyardMMO/Content/Data'


class MapRoomWorldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.world = json.loads((DATA / 'maproom-world.json').read_text())
        cls.cells = cls.world['cells']
        cls.lookup = {(cell['x'], cell['y']): cell for cell in cls.cells}

    def test_original_world_extent_and_unique_cells(self):
        self.assertEqual((self.world['width'], self.world['height']), (500, 500))
        self.assertEqual(len(self.lookup), len(self.cells))
        self.assertEqual(len(self.cells), 166325)
        self.assertTrue(all(0 <= x < 500 and 0 <= y < 500 for x, y in self.lookup))
        self.assertEqual(self.world['missingCell'], {'kind': 100, 'level': 0, 'terrain': 0})

    def test_recorded_source_generator_counts(self):
        counts = {}
        for cell in self.cells:
            counts[str(cell['kind'])] = counts.get(str(cell['kind']), 0) + 1
        self.assertEqual(counts, {'1': 23464, '100': 63131, '102': 10625, '103': 765, '104': 68340})
        self.assertEqual(counts, self.world['counts'])

    def test_actual_source_graph_hashes(self):
        self.assertEqual(len(self.world['sourceSha256']), 16)
        for name, digest in self.world['sourceSha256'].items():
            with self.subTest(source=name):
                self.assertEqual(hashlib.sha256((DATA / 'Original/MapRoom3' / name).read_bytes()).hexdigest(), digest)

    def test_authored_tiers_and_tribes(self):
        for cell in self.cells:
            kind, level = cell['kind'], cell['level']
            if kind in (102, 103):
                choices = [10, 20, 30, 40, 50] if kind == 102 else [30, 40, 50]
                self.assertEqual(level, choices[abs(cell['x'] * 73 + cell['y'] * 31) % len(choices)])
            elif kind == 104:
                self.assertIn(level, range(5, 51, 5))
            elif kind == 1:
                self.assertIn(level, (45, 50))
                self.assertEqual(cell['tribeIndex'], (cell['x'] + cell['y']) % 4)
            else:
                self.assertEqual(level, 0)
                self.assertTrue(32 <= cell['terrain'] <= 79)

    def test_fortifications_retain_adjacent_source_parents(self):
        for cell in self.cells:
            if cell['kind'] != 104:
                continue
            parent = self.lookup[(cell['parentX'], cell['parentY'])]
            self.assertIn(parent['kind'], (102, 103))
            x, y = parent['x'], parent['y']
            shift = y & 1
            neighbors = [(x+1, y), (x-1, y), (x+shift, y-1), (x+shift-1, y-1),
                         (x+shift, y+1), (x+shift-1, y+1)]
            self.assertIn((cell['x'], cell['y']), neighbors)

    def test_original_dependency_versions_and_integrity(self):
        root = PROJECT / 'Tools/MapRoomGeneration'
        manifest = json.loads((root / 'dependencies.json').read_text())
        self.assertEqual({name: value['version'] for name, value in manifest['packages'].items()},
                         {'alea': '1.0.1', 'simplex-noise': '4.0.3'})
        for name, digest in manifest['sha256'].items():
            self.assertEqual(hashlib.sha256((root / 'third_party' / name).read_bytes()).hexdigest(), digest)

    def test_complete_original_generator_reproduces_bundled_world(self):
        node = os.environ.get('CODEX_PRIMARY_RUNTIME_NODE') or shutil.which('node')
        if not node:
            self.skipTest('Optional regeneration requires Node 24 or newer')
        version = subprocess.check_output([node, '-p', 'process.versions.node'], text=True).strip()
        if int(version.split('.')[0]) < 24:
            self.skipTest('Optional TypeScript-source regeneration requires Node 24 or newer')
        result = subprocess.run([node, str(PROJECT / 'Tools/prepare_maproom_world.mjs'), '--check'],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report['passed'] and report['checked'])
        self.assertEqual(report['cells'], len(self.cells))


if __name__ == '__main__':
    unittest.main()
