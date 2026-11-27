"""Verify retained original rules and deterministic extraction, without Unreal.

These checks cover source provenance and conversion. They do not claim to run
UHT/UBT, render native UI, execute multiplayer commands or validate save I/O.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest

from prepare_store_rules import SOURCES, build_rules, parse_store, parse_yard


class WorkersStoreSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = Path(__file__).resolve().parents[1] / 'Plugins/BackyardMMO'
        cls.data = cls.plugin / 'Content/Data'
        cls.retained = cls.data / 'Original/Store'
        cls.rules = json.loads((cls.data / 'store-rules.json').read_text())
        cls.provenance = json.loads((cls.retained / 'provenance.json').read_text())
        cls.catalog = json.loads((cls.data / 'catalog.json').read_text())['buildings']

    def source(self, relative):
        return (self.retained / relative).read_text(encoding='utf-8-sig')

    def test_original_reference_hashes_and_lengths(self):
        self.assertEqual(set(self.provenance['sources']), set(SOURCES))
        for relative, metadata in self.provenance['sources'].items():
            with self.subTest(source=relative):
                raw = (self.retained / metadata['file']).read_bytes()
                self.assertEqual(len(raw), metadata['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), metadata['sha256'])

    def test_rules_reproduce_from_retained_original_sources(self):
        rebuilt = build_rules(self.retained, self.plugin / 'SourceAssets', self.catalog)
        self.assertEqual(rebuilt, self.rules)

    def test_ordinary_starting_shiny_uses_configuration_not_sandbox(self):
        config = self.source('server/src/config/GameConfig.ts')
        account = self.source('server/src/game-data/getDefaultBaseData.ts')
        self.assertRegex(config, r'\bshiny:\s*1500\b')
        self.assertIn('devSandbox: process.env.ENV === Env.PROD ? false : false', config)
        self.assertIn('credits: devConfig.shiny || 1000', account)
        self.assertEqual(self.rules['initialShiny'], 1500)
        self.assertEqual(self.rules['initialShinyUnsetFallback'], 1000)

    def test_workers_are_permanent_one_plus_four_source_store_purchases(self):
        queue = self.source('client/scripts/QUEUE.as')
        store = self.source('client/scripts/STORE.as')
        self.assertIn('param1 += STORE._storeData.BEW.q', queue)
        self.assertIn('QUEUE._workerCount >= 5', store)
        self.assertEqual(self.rules['initialWorkers'], 1)
        self.assertEqual(self.rules['workerCosts'], [250, 500, 1000, 2000])
        self.assertEqual(self.rules['maxWorkers'], 5)
        self.assertEqual(self.rules['outpostWorkers'], 1)

    def test_repair_does_not_reserve_worker_and_zero_time_build_bypasses_gate(self):
        foundation = self.source('client/scripts/BFOUNDATION.as')
        repair = foundation.split('public function Repair():void', 1)[1].split('public function Repaired()', 1)[0]
        self.assertIn('this._repairing = 1', repair)
        self.assertNotIn('QUEUE.Add', repair)
        self.assertNotIn('QUEUE.CanDo', repair)
        build = self.source('client/scripts/BASE.as').split('public static function addBuildingB(', 1)[1].split('private static function ConvertToInfernoBuilding', 1)[0]
        self.assertIn('costs[0].time.Get() == 0', build)
        self.assertIn('if (!_loc3_)', build)
        self.assertIn('QUEUE.CanDo().error == false', build)
        self.assertFalse(self.rules['workerRules']['repairRequiresWorker'])
        self.assertFalse(self.rules['workerRules']['zeroTimeConstructionRequiresWorker'])

    def test_mushroom_initial_population_and_regrowth_are_distinct(self):
        rules = self.rules['mushroomRules']
        self.assertEqual(rules['initial'], 20)
        self.assertEqual((rules['initialClusters'], rules['initialPerCluster']), (5, 4))
        self.assertEqual(rules['retainedCap'], 20)
        self.assertEqual(rules['regrowthCap'], 10)
        self.assertEqual(rules['maxCatchup'], 10)
        self.assertEqual(rules['interval'], 17280)
        source = self.source('client/scripts/MUSHROOMS.as')
        self.assertIn('spawnCount = 10 - mushroomCount', source)
        self.assertIn('BASE._lastSpawnedMushroom = GLOBAL.Timestamp()', source)

    def test_mushroom_clearing_calls_cadence_and_outer_collar_are_retained(self):
        rules = self.rules['mushroomRules']
        self.assertEqual((rules['pickCalls'], rules['displayHz'], rules['pickSeconds']), (62, 40, 1.55))
        self.assertTrue(rules['workerRequired'])
        self.assertEqual((rules['resourceCost'], rules['shinyCost']), (0, 0))
        self.assertEqual((rules['spawnMarginPixels'], rules['spawnAttempts'], rules['footprintPixels']), (200, 5000, 30))
        source = self.source('client/scripts/MUSHROOMS.as')
        self.assertIn('QUEUE.Add("mushroom" + param1._id, param1)', source)
        self.assertIn('200 + GLOBAL._mapWidth * 0.5', source)
        self.assertIn('!GRID.FootprintBlocked([new Rectangle(0, 0, 30, 30)]', source)
        shake = self.source('client/scripts/BMUSHROOM.as')
        self.assertIn('if (_shake > 60', shake)
        self.assertIn('++_shake', shake)

    def test_golden_reward_probabilities_match_both_client_and_server(self):
        rules = self.rules['mushroomRules']
        self.assertEqual((rules['goldenChanceNumerator'], rules['goldenChanceDenominator']), (1, 4))
        self.assertEqual(rules['goldenRewards'], [{'shiny': 3, 'weight': 2}, {'shiny': 8, 'weight': 1}])
        self.assertEqual(rules['ordinaryReward'], 0)
        self.assertFalse(rules['visualVariantDeterminesReward'])
        purchases = self.source('server/src/game-data/store/purchaseKeys.ts')
        self.assertRegex(purchases, r'"MUSHROOM1":\s*3')
        self.assertRegex(purchases, r'"MUSHROOM2":\s*8')
        self.assertRegex(purchases, r'"MUSHROOM3":\s*3')
        source = self.source('client/scripts/MUSHROOMS.as')
        self.assertIn('new Rndm(int(mushroom.x * mushroom.y))', source)
        self.assertIn('if (mushroomVariant == 3)', source)
        self.assertIn('mushroomVariant = 1', source)

    def test_expansion_prices_and_integer_dimension_rounding(self):
        self.assertEqual(self.rules['expansionCosts'], [50, 100, 150, 200, 250, 300])
        self.assertEqual(self.rules['expansionSourceDimensions'], [
            [1000, 800], [1100, 880], [1220, 980], [1340, 1080],
            [1480, 1180], [1620, 1300], [1780, 1420]])
        source = self.source('client/scripts/STORE.as')
        self.assertIn('GLOBAL._mapWidth *= 1.1', source)
        self.assertIn('GLOBAL._mapHeight *= 1.1', source)
        self.assertIn('GLOBAL._mapWidth = Math.ceil(GLOBAL._mapWidth / 20) * 20', source)
        global_source = self.source('client/scripts/GLOBAL.as')
        self.assertIn('public static var _mapWidth:int', global_source)
        self.assertIn('public static var _mapHeight:int', global_source)

    def test_normal_decorations_exclude_blocked_and_reward_only_entries(self):
        expected = (set(range(28, 51)) | set(range(55, 110))) - {58, 59, 67, 69, 70}
        self.assertEqual({row['type'] for row in self.rules['decorations']}, expected)
        self.assertEqual(len(expected), 73)
        yard = {row['id']: row for row in parse_yard(self.retained)}
        store = parse_store(self.retained)
        for row in self.rules['decorations']:
            with self.subTest(type=row['type']):
                self.assertFalse(yard[row['type']].get('block', False))
                self.assertEqual(row['price'], store[f"BUILDING{row['type']}"]['costs'][0])
                self.assertEqual(row['limit'], yard[row['type']]['quantity'][0])
                self.assertTrue((self.plugin / 'SourceAssets' / row['texture']).is_file())
        self.assertTrue(self.rules['decorationRules']['zeroLimitMeansUnlimited'])

    def test_packing_is_additive_and_applies_after_base_storage(self):
        self.assertEqual(self.rules['packingCosts'], list(range(50, 501, 50)))
        packing = self.rules['packingRules']
        self.assertEqual(packing['maxLevel'], 10)
        self.assertEqual(packing['capacityIncrementPerLevel'], .1)
        self.assertFalse(packing['compound'])
        source = self.source('client/scripts/STORE.as')
        self.assertIn('GLOBAL._upgradePacking += 0.1 * _storeData.BIP.q', source)
        base = self.source('client/scripts/BASE.as')
        self.assertIn('_resources["r" + _loc2_ + "max"] *= GLOBAL._upgradePacking', base)
        self.assertIn('Math.floor(_resources["r" + _loc2_ + "max"])', base)

    def test_speedup_quotes_preserve_normal_purchase_duration_thresholds(self):
        rules = self.rules['speedupRules']
        self.assertEqual((rules['SP1']['price'], rules['SP1']['maximumRemainingInclusive']), (0, 300))
        self.assertEqual((rules['SP2']['price'], rules['SP2']['seconds'], rules['SP2']['minimumRemainingInclusive']), (20, 3600, 3600))
        self.assertEqual((rules['SP3']['price'], rules['SP3']['seconds'], rules['SP3']['minimumRemainingInclusive']), (40, 7200, 7200))
        self.assertEqual(rules['SP4']['minimumRemainingExclusive'], 300)
        source = self.source('client/scripts/STORE.as')
        self.assertIn('_loc12_ < 60 * 60 && !_storeInventory.SP2', source)
        self.assertIn('_loc12_ < 60 * 60 * 2 && !_storeInventory.SP3', source)
        self.assertTrue(rules['completedWorkBypassesWorkerArrival'])

    def test_store_prices_override_stale_catalog_r5_and_labels_are_localized(self):
        decorations = {row['type']: row for row in self.rules['decorations']}
        self.assertEqual((decorations[55]['price'], decorations[55]['sourceCatalogR5']), (5, 150))
        self.assertEqual((decorations[49]['price'], decorations[49]['sourceCatalogR5']), (125, 250))
        self.assertEqual((decorations[68]['price'], decorations[68]['sourceCatalogR5']), (250, 150))
        self.assertEqual(self.rules['decorationCategories'], ['War', 'Nature', 'Leisure', 'Flags', 'Special'])
        button = self.source('client/scripts/BUILDINGBUTTON.as')
        self.assertIn('STORE._storeItems["BUILDING" + this._id].c[0]', button)


if __name__ == '__main__':
    unittest.main(verbosity=2)
