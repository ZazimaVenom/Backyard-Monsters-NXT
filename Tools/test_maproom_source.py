"""Original MR3 conversion/provenance checks; these do not execute Unreal."""
import hashlib
import json
from pathlib import Path
import unittest

from prepare_maproom_rules import extract


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'Plugins/BackyardMMO/Content/Data'
RULES = json.loads((DATA / 'maproom-rules.json').read_text())
ORIGINAL = DATA / 'Original/MapRoom3'


class MapRoomSourceTests(unittest.TestCase):
    def test_all_source_references_are_exact(self):
        for entry in RULES['references']:
            with self.subTest(path=entry['path']):
                self.assertEqual(hashlib.sha256((ORIGINAL / entry['path']).read_bytes()).hexdigest(), entry['sha256'])

    def test_conversion_reproduces_bundled_rules_without_execution(self):
        self.assertEqual(extract(ORIGINAL), RULES)

    def test_mr3_branch_topology_and_capture_types(self):
        self.assertEqual((RULES['map']['width'], RULES['map']['height']), (500, 500))
        self.assertEqual(RULES['map']['hexLayout'], 'odd-r')
        self.assertEqual(RULES['map']['minimumHomeDistance'], 7)
        self.assertEqual(RULES['attack']['capturableTypes'], [102, 103, 104])
        self.assertEqual(RULES['attack']['captureDamagePercent'], 90)
        self.assertEqual(RULES['tribeRegenerationSeconds'], 259200)

    def test_source_rates_ranges_and_fortification_curve(self):
        self.assertEqual(list(RULES['resourceProductionRates'].values()), [1, 2, 5, 11, 23])
        self.assertEqual(list(RULES['resourceCapacities'].values()), [175000, 350000, 750000, 2000000, 4000000])
        self.assertEqual(list(RULES['structureRanges']['102'].values()), [2, 3, 4, 5, 6])
        self.assertEqual(list(RULES['structureRanges']['103'].values()), [10, 15, 20])
        self.assertEqual(RULES['fortificationDamageReduction'], [0, 7, 13, 23, 35, 50, 75])
        self.assertEqual(RULES['homeRange'], 10)

    def test_source_attack_costs_and_site_management(self):
        self.assertEqual(RULES['attack']['townHallCostMultipliers'], [0, 100, 200, 400, 800, 1600, 4000, 10000, 25000, 75000, 225000])
        self.assertEqual(RULES['attack']['paidResources'], ['twigs', 'pebbles', 'putty'])
        self.assertFalse(RULES['siteManagement']['construct'])
        self.assertFalse(RULES['siteManagement']['upgrade'])
        self.assertTrue(RULES['siteManagement']['repair'])
        self.assertEqual(RULES['siteManagement']['repairAllMinimumHealthFraction'], .25)

    def test_all_authored_templates_preserve_pixels_and_rows(self):
        templates = RULES['templates']
        self.assertEqual(len(templates), 26)
        self.assertEqual(sum(len(row['buildings']) for row in templates), 8845)
        self.assertEqual(len({row['key'] for row in templates}), 26)
        fractional = 0
        for template in templates:
            for building in template['buildings']:
                source = building['source']
                self.assertEqual((building['sourceX'], building['sourceY']), (source['X'], source['Y']))
                self.assertEqual(building['type'], source['t'])
                self.assertEqual(building['level'], source.get('l', 1))
                fractional += source['X'] % 20 != 0 or source['Y'] % 20 != 0
        self.assertGreater(fractional, 5000)

    def test_core_levels_and_untargetable_resource(self):
        cores = {row['id']: row for row in RULES['coreBuildings']}
        self.assertEqual(cores[138]['hp'], [400000, 500000, 600000])
        self.assertEqual(cores[140]['hp'], [8800, 42000, 200000, 400000, 600000])
        self.assertTrue(cores[139]['isUntargetable'])
        self.assertEqual(cores[139]['hp'], [1])
        self.assertEqual(cores[138]['footprintSourcePixels'], [130, 130])
        high_defender = next(row for row in RULES['templates'] if row['type'] == 104 and row['level'] == 50)
        self.assertEqual(high_defender['buildings'][0]['type'], 140)
        self.assertEqual(high_defender['buildings'][0]['level'], 5)

    def test_stronghold_emitters_are_extracted_from_original_source(self):
        combat = RULES['strongholdCombat']
        self.assertEqual(combat['buildingType'], 138)
        self.assertEqual([row['offset'] for row in combat['intactEmitters']],
                         [[-.5, -68], [58, -37], [-.5, -7], [-59, -40]])
        self.assertEqual([row['offset'] for row in combat['damagedEmitters']],
                         [[-.5, -68], [58, -26], [-6, -7], [-67, -53]])
        self.assertEqual([row['angle'] for row in combat['intactEmitters']],
                         [[-180, 0], [-90, 90], [0, 180], [90, 180]])
        self.assertEqual([row['absoluteAngle'] for row in combat['intactEmitters']], [False, False, False, True])
        self.assertEqual(combat['attackRateUnit'], 'seconds')
        self.assertFalse(combat['healthDamageScale'])
        self.assertFalse(combat['acquisitionRadiusInclusive'])
        self.assertTrue(combat['retainTargetOutsideRangeAndArc'])


if __name__ == '__main__':
    unittest.main()
