"""Source experience data tests; independent of Unreal and game save files."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest

from prepare_experience import parse_integer_table


class ExperiencePipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = Path(__file__).resolve().parents[1] / 'Plugins/BackyardMMO'
        cls.data = json.loads((cls.plugin / 'Content/Data/experience.json').read_text())

    def test_full_multiline_active_client_table(self):
        client = (self.plugin / 'Content/Data/Original/Experience/BASE.as').read_text(encoding='utf-8-sig')
        original = parse_integer_table(client, 'private static const s_levels:Array = [')
        self.assertEqual(self.data['thresholds'], original)
        self.assertEqual(len(original), 100)
        self.assertEqual(original[-1], 755984118210000)
        self.assertEqual(self.data['maximumDisplayedLevel'], 99)

    def test_compiled_constants_are_identical_to_original_data(self):
        compiled = (self.plugin / 'Source/BackyardMMO/Private/BYMExperienceThresholds.inl').read_text()
        values = [int(value) for value in re.findall(r'\b(\d+)LL\b', compiled)]
        self.assertEqual(values, self.data['thresholds'])
        self.assertTrue(all(a < b for a, b in zip(values, values[1:])))
        self.assertLess(values[-1], 2 ** 53)

    def test_server_discrepancy_and_exact_reference_hashes(self):
        self.assertEqual(len(self.data['suppliedServerThresholds']), 56)
        self.assertEqual(self.data['suppliedServerThresholds'], self.data['thresholds'][:56])
        for relative, metadata in self.data['sources'].items():
            local = self.plugin / 'Content/Data/Original/Experience' / Path(relative).name
            raw = local.read_bytes()
            self.assertEqual(len(raw), metadata['bytes'])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), metadata['sha256'])

    def test_parser_rejects_executable_or_invalid_tables(self):
        for source in ('marker[0, run()]', 'marker[0, 3-1]', 'marker[-1,2]', 'marker[0,2,2]', 'marker[1,2]', 'marker[0,9007199254740992]'):
            with self.assertRaises(ValueError):
                parse_integer_table(source, 'marker[')
        self.assertEqual(parse_integer_table('marker[0, // comment\n2, /* comment */ 3]', 'marker['), [0, 2, 3])


if __name__ == '__main__':
    unittest.main(verbosity=2)
