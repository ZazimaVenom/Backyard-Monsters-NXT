"""Check the selected original Bunker source branch and retained provenance.

These tests do not execute Unreal, the server command validator or combat.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest


class BunkerSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = Path(__file__).resolve().parents[1] / 'Plugins/BackyardMMO/Content/Data/Original/Bunker'
        cls.manifest = json.loads((cls.original / 'provenance.json').read_text())

    def original_text(self, name: str) -> str:
        return (self.original / name).read_text(encoding='utf-8-sig')

    def test_all_exact_original_reference_hashes(self):
        self.assertEqual(len(self.manifest['sources']), 11)
        for source, metadata in self.manifest['sources'].items():
            with self.subTest(source=source):
                raw = (self.original / metadata['file']).read_bytes()
                self.assertEqual(len(raw), metadata['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), metadata['sha256'])

    def test_ordinary_capacity_is_the_runtime_override(self):
        global_source = self.original_text('GLOBAL.as')
        override = global_source.split('private static function changeNotMaproom3SpecificBuildings()', 1)[1].split('public static function SetBuildingProps()', 1)[0]
        raw = re.search(r'_buildingProps\[21\]\.capacity\s*=\s*\[([^\]]+)\]', override)
        self.assertIsNotNone(raw)
        capacity = [int(value.strip()) for value in raw.group(1).split(',')]
        self.assertEqual(capacity, self.manifest['rules']['capacity'])
        self.assertEqual(capacity, [380, 450, 540, 660, 800])
        selector = global_source.split('public static function SetBuildingProps()', 1)[1].split('public static function isInfernoMode', 1)[0]
        self.assertIn('if (!MapRoomManager.instance.isInMapRoom3)', selector)
        self.assertIn('changeNotMaproom3SpecificBuildings();', selector)
        yard = self.original_text('YARD_PROPS.as').split('"id": 22,', 1)[1].split('"id": 23,', 1)[0]
        raw_capacity = [int(value.strip()) for value in re.search(r'"capacity":\s*\[([^\]]+)\]', yard).group(1).split(',')]
        self.assertEqual(raw_capacity, self.manifest['rules']['rawMapRoom3Capacity'])
        self.assertEqual([value * 2 for value in raw_capacity], capacity)
        ranges = [int(value) for value in re.findall(r'"range":\s*(\d+)', yard)]
        self.assertEqual(ranges, self.manifest['rules']['triggerRangePixels'])

    def test_original_transfer_whitelist_excludes_flyers_and_healers(self):
        popup = self.original_text('MONSTERBUNKERPOPUP.as')
        whitelist = popup.split('private const BUNKERABLE_MONSTERS:Object = {', 1)[1].split('};', 1)[0]
        overworld = re.findall(r'"(C\d+)"\s*:\s*1', whitelist)
        self.assertEqual(overworld, self.manifest['rules']['allowedSpecies'])
        self.assertEqual(overworld, [f'C{index}' for index in range(1, 14)] + ['C17'])
        self.assertNotIn('C14', overworld)
        self.assertNotIn('C15', overworld)
        self.assertNotIn('C16', overworld)
        # The int-returning GetCost rounds each species batch before Transfer
        # sums it. Preserve that original branch, not premium-store pricing.
        cost = popup.split('private function GetCost(', 1)[1].split('private function SelectAdd', 1)[0]
        self.assertIn('):int {', cost)
        self.assertIn('CREATURES.GetProperty(param1, "cResource", 0, true) * 0.5 * param2', cost)
        transfer = popup.split('private function Transfer(', 1)[1].split('private function Check(', 1)[0]
        self.assertIn('_loc3_.Add(this.GetCost(_loc2_, this._selected[_loc2_].Get()))', transfer)
        self.assertIn('BASE.Charge(3, _loc3_.Get(), false)', transfer)


if __name__ == '__main__':
    unittest.main(verbosity=2)
