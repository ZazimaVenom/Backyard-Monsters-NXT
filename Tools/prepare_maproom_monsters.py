#!/usr/bin/env python3
"""Extract the active MR3 monster data; never enable the dormant rebalance table."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
import re
import shutil
from pathlib import Path

SOURCE_FILES = (
    'server/src/game-data/stats/monsterStats.ts',
    'server/src/services/maproom/validateAttack.ts',
    'client/scripts/CREATURELOCKER.as',
    'client/scripts/CREATURES.as',
    'client/scripts/GLOBAL.as',
    'client/scripts/YARD_PROPS.as',
    'client/scripts/HOUSING.as',
    'client/scripts/BUILDING15.as',
    'client/scripts/BUILDING22.as',
    'client/scripts/PersistentMonsterBunker.as',
    'client/scripts/MONSTERBUNKER.as',
    'client/scripts/com/monsters/player/MonsterData.as',
    'client/scripts/com/monsters/player/CreepInfo.as',
    'client/scripts/com/monsters/monsters/creeps/rebalance/RebalancedCreatures.as',
)
FIELDS = {'hp': ('health', 0), 'damage': ('damage', 0), 'speed': ('speed', 0),
          'range': ('range', 0), 'housing': ('cStorage', 0), 'goo': ('cResource', 0),
          'time': ('cTime', 0), 'targetGroup': ('targetGroup', 1),
          'attackDelayFrames': ('attackDelay', 0), 'healTime': ('hTime', 0),
          'healGoo': ('hResource', 0), 'bucket': ('bucket', 0)}


def after_literal(text: str, marker: str):
    from prepare_assets import LiteralParser
    return LiteralParser(text.split(marker, 1)[1]).value()


def active_client_props(props: dict) -> dict:
    """CREATURELOCKER.modifyCreepData, including each original healing array length."""
    result = copy.deepcopy(props)
    for field in ('cResource', 'cTime'):
        result[field] = [value*3 for value in result[field]]
    if 'hResource' in result:
        for healing, hatching in (('hResource', 'cResource'), ('hTime', 'cTime')):
            result[healing] = [result[hatching][min(index, len(result[hatching])-1)]*.25
                               for index in range(len(result[healing]))]
    return result


def extract(source: Path) -> dict:
    texts = {path: (source/path).read_text(encoding='utf-8-sig') for path in SOURCE_FILES}
    locker = texts['client/scripts/CREATURELOCKER.as']
    flag = re.search(r'k_USE_REBALANCED_MONSTERS:Boolean\s*=\s*(true|false)', locker)
    if not flag or flag[1] != 'false':
        raise ValueError('Source rebalance activation changed; audit the active MR3 branch before converting')
    # Fail instead of silently retaining stale interpretation if the authoritative
    # transformations change. Full source hashes remain in the generated report.
    for fragment in ('props.cResource[_loc3_] *= 3', 'props.cTime[_loc3_] *= 3',
                     'props.hResource[_loc3_] = 0.25 *', 'props.hTime[_loc3_] = 0.25 *'):
        if fragment not in locker: raise ValueError('MR3 stat transformation changed: '+fragment)
    client = after_literal(locker, '_mainCreatures = ')
    server = after_literal(texts['server/src/game-data/stats/monsterStats.ts'],
                           'export const mr3MonsterStats: MR3MonsterStatsMap = ')
    validator = texts['server/src/services/maproom/validateAttack.ts']
    if 'isInMaproom3 ? mr3MonsterStats[id]?.props : monsterStats[id].props' not in validator:
        raise ValueError('Source server MR3 validation branch changed')
    monsters = []
    compared = 0
    for identifier, entry in server.items():
        props = entry['props']
        active = active_client_props(client[identifier]['props'])
        for field, values in props.items():
            if not isinstance(values, list) or not values or any(
                    isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
                    for value in values):
                raise ValueError(f'Invalid numeric MR3 props {identifier}.{field}')
            if active.get(field) != values:
                raise ValueError(f'Active client/server MR3 disagreement: {identifier}.{field}')
            compared += len(values)
        levels = []
        max_level = len(client[identifier].get('trainingCosts', []))+1
        for number in range(1, max_level+1):
            level = {'level': number}
            for field, (source_field, default) in FIELDS.items():
                values = props.get(source_field, [default])
                level[field] = values[min(number-1, len(values)-1)]
            levels.append(level)
        monsters.append({'id': identifier, 'levels': levels, 'sourceProps': props,
                         'maxLevel': max_level,
                         'sourceBlocked': bool(client[identifier].get('blocked', False)),
                         'sourceFake': bool(client[identifier].get('fake', False)),
                         'abilityProperties': {key: value for key, value in props.items()
                                               if key not in {pair[0] for pair in FIELDS.values()}}})
    yard = after_literal(texts['client/scripts/YARD_PROPS.as'], '_yardProps:Array = ')
    bunkerable = after_literal(texts['client/scripts/PersistentMonsterBunker.as'],
                              'BUNKERABLE_MONSTERS:Object = ')
    globals_text = texts['client/scripts/GLOBAL.as']
    ordinary_capacity = {}
    for identifier in (5, 15, 22):
        ordinary_capacity[str(identifier)] = after_literal(globals_text, f'_buildingProps[{identifier-1}].capacity = ')
    return {
        'schema': 1, 'mapRoomVersion': 3,
        'selection': {'sourceRebalancedMonstersEnabled': False,
                      'sourceContext': 'MapRoomManager.instance.isInMapRoom3; the active client transforms monster costs globally in MR3, not only during combat.',
                      'sourceServerValidation': 'server/src/services/maproom/validateAttack.ts:expectedProps',
                      'hatchCostMultiplier': 3, 'hatchTimeMultiplier': 3,
                      'healingFractionOfTransformedHatchCost': .25,
                      'healingFractionOfTransformedHatchTime': .25,
                      'shortArrayPolicy': 'CREATURES.GetProperty clamps each property independently to its final array value.',
                      'excludedClientEntries': sorted(set(client)-set(server)),
                      'excludedReason': 'C200 is the blocked source AILooter1 entry; the MR3 server monster validation table does not contain it.'},
        'monsters': monsters,
        'crossCheck': {'matchingMonsters': len(monsters), 'matchingNumericValues': compared,
                       'clientServerMismatches': [],
                       'method': 'Exact comparison of all server MR3 property arrays against active client literal data after modifyCreepData.'},
        'housing': {'buildingId': 15, 'capacityByLevel': yard[14]['capacity'],
                    'ordinaryCapacityByLevel': ordinary_capacity['15'],
                    'completedHousingRetainsCapacityWhenDestroyed': True,
                    'destructionResidentHealthMultiplier': .5,
                    'usedCapacityIncludesBunkerReservations': True,
                    'capacitySource': 'GLOBAL.SetBuildingProps chooses pristine YARD_PROPS for MR3; HOUSING.HousingSpace counts completed buildings regardless of HP.',
                    'destructionSource': 'BUILDING15.Destroyed halves resident monster HP and skips ordinary Cull/RemoveHouse.'},
        'flinger': {'buildingId': 5, 'capacityByLevel': yard[4]['capacity'],
                    'ordinaryCapacityByLevel': ordinary_capacity['5']},
        'bunker': {'buildingId': 22, 'capacityByLevel': yard[21]['capacity'],
                   'ordinaryCapacityByLevel': ordinary_capacity['22'],
                   'capacityUnit': 'housing_space', 'eligibleMonsters': list(bunkerable),
                   'transferPuttyCost': 0, 'transferRequiresFullHealth': True,
                   'returnToHousingRequiresFullHealth': True,
                   'transferModel': 'Reserve persistent owned monster with ownerID=bunker ID; return sets ownerID=0. Inventory count remains unchanged.',
                   'mainYardPersistence': True, 'nonMainYardRepresentation': 'ordinary_counts',
                   'minimumDeploymentHealthFractionExclusive': .1,
                   'countAvailableHealthFractionInclusive': .1,
                   'destructionResidentHealthMultiplier': .5,
                   'individualSourceFields': ['health', 'ownerID', 'queued', 'self'],
                   'persistentFieldNotes': 'self is a transient MonsterBase reference, not serialized state. Health, owner reservation and healing queue require host-owned persistence.',
                   'source': 'PersistentMonsterBunker; BUILDING22; MonsterData.reserve/release; CreepInfo'},
        'scope': ['Exact original numerical data does not implement specialized monster abilities by itself.',
                  'Negative damage and additional ability properties are retained as source data; no new combat ability is inferred.',
                  'Original research/unlock rules and ordinary catalog values remain unchanged.',
                  'Housing/Bunker persistence rules are source requirements, not a claim that all runtime persistence is implemented.'],
        'sourceReferences': [{'path': path, 'sha256': hashlib.sha256((source/path).read_bytes()).hexdigest()}
                             for path in SOURCE_FILES],
    }


def prepare(source: Path, data: Path) -> dict:
    result = extract(source)
    for path in SOURCE_FILES:
        destination = data/'Original/MapRoom3Monsters'/path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source/path, destination)
    (data/'maproom-monsters.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    result = prepare(args.source, args.project/'Plugins/BackyardMMO/Content/Data')
    print(json.dumps(result['crossCheck']))
