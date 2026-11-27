"""Cross-source checks for the active MR3 monster branch and source-only persistence rules."""
import hashlib
import json
from pathlib import Path
import unittest
from prepare_maproom_monsters import extract, active_client_props

DATA=Path(__file__).resolve().parents[1]/'Plugins/BackyardMMO/Content/Data'
SOURCE=DATA/'Original/MapRoom3Monsters'


class MapRoomMonsterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=json.loads((DATA/'maproom-monsters.json').read_text())
        cls.entries={entry['id']:entry for entry in cls.data['monsters']}
        cls.catalog={entry['id']:entry for entry in json.loads((DATA/'catalog.json').read_text())['monsters']}

    def test_retained_sources_reproduce_data_and_match_original_hashes(self):
        self.assertEqual(extract(SOURCE),self.data)
        for reference in self.data['sourceReferences']:
            self.assertEqual(hashlib.sha256((SOURCE/reference['path']).read_bytes()).hexdigest(),reference['sha256'])

    def test_active_client_and_server_numeric_arrays_agree(self):
        self.assertEqual(len(self.entries),27)
        self.assertEqual(self.data['crossCheck']['matchingNumericValues'],1008)
        self.assertEqual(self.data['crossCheck']['clientServerMismatches'],[])
        self.assertFalse(self.data['selection']['sourceRebalancedMonstersEnabled'])
        self.assertEqual(self.data['selection']['excludedClientEntries'],['C200'])
        self.assertTrue(self.catalog['C200']['sourceBlocked'])
        self.assertEqual(self.catalog['C200']['name'],'AILooter1')
        for identifier,entry in self.entries.items():
            active=active_client_props(self.catalog[identifier]['sourceProps'])
            for field,values in entry['sourceProps'].items():
                self.assertEqual(active[field],values,(identifier,field))

    def test_every_level_preserves_combat_and_triples_only_hatching(self):
        combat=('hp','damage','speed','range','housing','targetGroup','attackDelayFrames')
        for identifier,entry in self.entries.items():
            ordinary=self.catalog[identifier]
            self.assertEqual(len(entry['levels']),len(ordinary['levels']))
            for mr3,base in zip(entry['levels'],ordinary['levels']):
                for field in combat:self.assertEqual(mr3[field],base[field],(identifier,mr3['level'],field))
                self.assertEqual(mr3['goo'],base['goo']*3)
                self.assertEqual(mr3['time'],base['time']*3)
        self.assertEqual(self.entries['C1']['levels'][0]['hp'],200)
        self.assertEqual(self.entries['C1']['levels'][0]['damage'],60)
        self.assertEqual(self.entries['C1']['levels'][0]['goo'],750)

    def test_short_arrays_healing_and_fractional_values_are_not_rounded(self):
        pokey=self.entries['C1']['levels']
        self.assertEqual([row['healGoo'] for row in pokey],[187.5,337.5,506.25,600,750,937.5])
        self.assertEqual([row['healTime'] for row in pokey],[11.25,7.5,6,5.25,4.5,3.75])
        octo=self.entries['C2']['levels']
        self.assertEqual([row['time'] for row in octo],[45,48,48,48,48,48])
        self.assertEqual([row['healTime'] for row in octo],[11.25]*6)
        self.assertNotEqual(octo[-1]['healTime'],octo[-1]['time']*.25)
        teratorn=self.entries['C14']['levels']
        self.assertEqual(teratorn[-1]['speed'],teratorn[-2]['speed'])

    def test_special_properties_stay_data_and_do_not_unlock_blocked_monsters(self):
        self.assertEqual(self.entries['C5']['abilityProperties'],{'explode':[1]})
        self.assertEqual(self.entries['C17']['abilityProperties'],{'splits':[2,2,3,3,4,5]})
        self.assertEqual(self.entries['C19']['abilityProperties']['resurrectCooldown'],[7,7,6,6,5,4])
        self.assertLess(self.entries['C15']['levels'][-1]['damage'],0)
        for identifier,entry in self.entries.items():
            self.assertEqual(entry['sourceBlocked'],self.catalog[identifier]['sourceBlocked'])
            self.assertEqual(entry['sourceFake'],self.catalog[identifier]['sourceFake'])
        self.assertEqual(self.entries['C19']['levels'][0]['healTime'],0)
        self.assertNotIn('hTime',self.entries['C19']['sourceProps'])

    def test_mr3_building_capacities_do_not_use_ordinary_runtime_overrides(self):
        housing,bunker,flinger=(self.data[key] for key in ('housing','bunker','flinger'))
        self.assertEqual(housing['capacityByLevel'],[250,425,520,670,740,870,1090,1225,1440,1680])
        self.assertEqual(housing['ordinaryCapacityByLevel'],[200,260,320,380,450,540])
        self.assertEqual(bunker['capacityByLevel'],[190,225,270,330,400])
        self.assertEqual(bunker['ordinaryCapacityByLevel'],[380,450,540,660,800])
        self.assertEqual(flinger['capacityByLevel'],[250,850,1500,2500,3500,3500,3500])

    def test_persistent_housing_and_bunker_require_individual_health_and_reservations(self):
        housing,bunker=self.data['housing'],self.data['bunker']
        self.assertTrue(housing['completedHousingRetainsCapacityWhenDestroyed'])
        self.assertTrue(housing['usedCapacityIncludesBunkerReservations'])
        self.assertEqual(housing['destructionResidentHealthMultiplier'],.5)
        self.assertEqual(bunker['destructionResidentHealthMultiplier'],.5)
        self.assertEqual(bunker['transferPuttyCost'],0)
        self.assertTrue(bunker['transferRequiresFullHealth'])
        self.assertTrue(bunker['returnToHousingRequiresFullHealth'])
        self.assertEqual(set(bunker['eligibleMonsters']),{f'IC{i}' for i in range(1,9)}|{f'C{i}' for i in range(1,14)}|{'C17'})
        self.assertEqual(bunker['minimumDeploymentHealthFractionExclusive'],.1)
        self.assertEqual(bunker['countAvailableHealthFractionInclusive'],.1)
        self.assertEqual(bunker['individualSourceFields'],['health','ownerID','queued','self'])
        self.assertEqual(bunker['nonMainYardRepresentation'],'ordinary_counts')


if __name__=='__main__':unittest.main()
