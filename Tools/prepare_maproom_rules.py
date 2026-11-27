#!/usr/bin/env python3
"""Extract the supplied Map Room 3 rules and all authored site layouts.

Only restricted data literals are read; no source script is executed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

TYPES = {'PLAYER': 101, 'RESOURCE': 102, 'STRONGHOLD': 103, 'FORTIFICATION': 104, 'OUTPOST': 1}
REFERENCES = (
    'server/src/config/MapRoom3Config.ts', 'server/src/config/NeighbourConfig.ts',
    'server/src/enums/MapRoom.ts', 'server/src/enums/EnumYardType.ts',
    'server/src/services/maproom/v3/generateCells.ts',
    'server/src/services/maproom/v3/calculateStructureLevel.ts',
    'server/src/services/maproom/v3/utils/isValidPosition.ts',
    'server/src/services/maproom/v3/getHexNeighborOffsets.ts',
    'server/src/services/maproom/v3/getDefenderCoords.ts',
    'server/src/services/maproom/v3/getDefenderLevels.ts',
    'server/src/services/maproom/v3/findFreeSector.ts',
    'server/src/services/maproom/v3/tribeSaveV3.ts',
    'server/src/services/maproom/v3/takeoverCellMR3.ts',
    'server/src/services/maproom/v3/joinNewWorldMap.ts',
    'server/src/services/maproom/v3/leavePosition.ts',
    'server/src/services/maproom/v3/removeDefenders.ts',
    'server/src/controllers/maproom/setMapVersion.ts',
    'server/src/controllers/maproom/v3/getCells.ts',
    'server/src/controllers/maproom/v3/relocate.ts',
    'server/src/controllers/maproom/v3/getFriendInfo.ts',
    'server/src/schemas/RelocateSchema.ts',
    'server/src/services/maproom/v3/getFriendYards.ts',
    'server/src/services/maproom/attackPermission.ts',
    'server/src/services/maproom/attackHistory.ts',
    'server/src/services/maproom/v3/createCellData.ts',
    'server/src/controllers/maproom/v3/cells/playerCell.ts',
    'server/src/controllers/maproom/v3/cells/wildMonsterCell.ts',
    'server/src/controllers/maproom/v3/cells/tribeOutpostCell.ts',
    'server/src/enums/Tribes.ts',
    'server/src/controllers/base/load/baseLoad.ts',
    'server/src/controllers/base/load/modes/baseModeAttack.ts',
    'server/src/controllers/base/save/baseSave.ts',
    'server/src/services/base/canAttack.ts', 'server/src/services/base/mapSaveData.ts',
    'client/scripts/com/monsters/maproom3/MapRoom3Cell.as',
    'client/scripts/com/monsters/maproom3/MapRoom3CellGraphic.as',
    'client/scripts/com/monsters/maproom3/data/MapRoom3CellData.as',
    'client/scripts/com/monsters/maproom3/tiles/MapRoom3TileSetManager.as',
    'client/scripts/com/monsters/maproom_manager/MapRoomManager.as',
    'client/scripts/BUILDINGS.as', 'client/scripts/BUILDINGINFO.as',
    'client/scripts/BUILDING11.as', 'client/scripts/BFOUNDATION.as', 'client/scripts/BASE.as',
    'client/scripts/GuardTower.as', 'client/scripts/ResourceOutpost.as', 'client/scripts/OutpostDefender.as',
    'client/scripts/BTOWER.as', 'client/scripts/Targeting.as', 'client/scripts/GRID.as',
    'client/scripts/com/monsters/pathing/PATHING.as',
    'client/scripts/com/monsters/utils/MathUtils.as',
    'client/scripts/YARD_PROPS.as',
    'client/scripts/com/monsters/ai/WMBASE.as', 'client/scripts/STORE.as',
    'client/scripts/com/monsters/baseBuffs/buffs/BuildingDefenseBuff.as',
    'client/scripts/com/monsters/baseBuffs/buffs/TowerDamageBuff.as',
    'client/scripts/com/monsters/baseBuffs/buffs/monsterDamageBuffs/MonsterAttackDamageBuff.as',
    'client/scripts/com/monsters/baseBuffs/buffs/monsterDamageBuffs/MonsterDamageBuff.as',
    'server/src/game-data/tribes/v3/resources.ts',
    'server/src/game-data/tribes/v3/strongholds.ts',
    'server/src/game-data/tribes/v3/defenders.ts',
    'server/src/game-data/tribes/v3/outposts/legionnaire.ts',
    'server/src/game-data/tribes/v3/outposts/kozu.ts',
    'server/src/game-data/tribes/v3/outposts/abunakki.ts',
    'server/src/game-data/tribes/v3/outposts/dreadnaught.ts',
)


def read(source: Path, relative: str) -> str:
    return (source / relative).read_text(encoding='utf-8-sig')


def literal(raw: str):
    from prepare_assets import LiteralParser
    return LiteralParser(raw).value()


def exported(raw: str, name: str):
    match = re.search(r'export\s+const\s+' + re.escape(name) + r'\b[^=]*=\s*', raw)
    if not match:
        raise ValueError('Missing source constant: ' + name)
    value = raw[match.end():]
    for key, number in TYPES.items():
        value = value.replace('[EnumYardType.' + key + ']', '"' + str(number) + '"')
    return literal(value)


def extract(source: Path) -> dict:
    config = read(source, 'server/src/config/MapRoom3Config.ts')
    enum = read(source, 'server/src/enums/MapRoom.ts')
    size = re.search(r'export enum MapRoom3\s*\{([^}]+)\}', enum, re.S)
    if not size:
        raise ValueError('MapRoom3 enum missing')
    dimensions = {key: int(value) for key, value in re.findall(r'(WIDTH|HEIGHT|MAX_PLAYERS)\s*=\s*(\d+)', size.group(1))}
    cell = read(source, 'client/scripts/com/monsters/maproom3/MapRoom3Cell.as')
    multipliers = literal(cell.split('ATTACK_COST_MULTIPLIERS_BY_TOWN_HALL_LEVEL:Array =', 1)[1])
    graphic = read(source, 'client/scripts/com/monsters/maproom3/MapRoom3CellGraphic.as')
    hex_size = {key: int(value) for key, value in re.findall(r'const\s+(HEX_WIDTH|HEX_HEIGHT|HEX_HEIGHT_OVERLAP|HEX_EDGE_LENGTH):Number\s*=\s*(\d+)', graphic)}
    neighbor = read(source, 'server/src/config/NeighbourConfig.ts')
    yard = read(source, 'client/scripts/YARD_PROPS.as')
    guard = read(source, 'client/scripts/GuardTower.as')
    special = re.search(r'k_SPECIAL_ANGLE:Point\s*=\s*new Point\(([-\d.]+),\s*([-\d.]+)\)', guard)
    if not special:
        raise ValueError('Stronghold special emitter arc missing')
    emitter_sets = []
    for variable in ('m_teslaPositions', 'm_teslaDamagedPositions'):
        block = re.search(re.escape(variable) + r'\s*=\s*Vector\.<TeslaData>\(\[(.*?)\]\)', guard, re.S)
        if not block:
            raise ValueError('Stronghold emitter positions missing: ' + variable)
        matches = re.findall(
            r'new TeslaData\(new Point\(([-\d.]+),\s*([-\d.]+)\),\s*'
            r'(?:new Point\(([-\d.]+),\s*([-\d.]+)\)|(k_SPECIAL_ANGLE))\)', block.group(1))
        if len(matches) != 4:
            raise ValueError('Expected exactly four Stronghold emitters: ' + variable)
        emitter_sets.append([{
            'offset': [float(x), float(y)],
            'angle': [float(special.group(1)), float(special.group(2))] if special_angle else [float(a), float(b)],
            'absoluteAngle': bool(special_angle),
        } for x, y, a, b, special_angle in matches])
    marker = re.search(r'_[A-Za-z_]+\s*:\s*Array\s*=\s*', yard)
    if not marker:
        raise ValueError('YARD_PROPS literal missing')
    rows = literal(yard[marker.end():])
    core = []
    for row in rows:
        if row.get('id') in (138, 139, 140):
            core.append({key: row[key] for key in ('id', 'name', 'type', 'hp', 'repairTime', 'stats', 'rps', 'isUntargetable', 'isImmobile') if key in row})
            core[-1]['footprintSourcePixels'] = [130, 130]
    templates = []
    groups = [('resources', 102, -1), ('strongholds', 103, -1), ('defenders', 104, -1),
              ('outposts/legionnaire', 1, 0), ('outposts/kozu', 1, 1),
              ('outposts/abunakki', 1, 2), ('outposts/dreadnaught', 1, 3)]
    for group, yard_type, tribe in groups:
        relative = 'server/src/game-data/tribes/v3/' + group + '.ts'
        data = exported(read(source, relative), group.split('/')[-1])
        for level, template in data.items():
            buildings = []
            for identity, row in template['buildingdata'].items():
                if not all(key in row for key in ('t', 'X', 'Y')):
                    raise ValueError('Incomplete authored building: ' + relative + ':' + identity)
                buildings.append({'id': identity, 'type': int(row['t']), 'level': int(row.get('l', 1)),
                                  'sourceX': row['X'], 'sourceY': row['Y'], 'source': row})
            templates.append({'key': group.replace('/', '_') + '_' + level, 'type': yard_type,
                              'level': int(level), 'tribeIndex': tribe, 'sourceFile': relative,
                              'buildings': buildings, 'monsters': template.get('monsters', {}),
                              'resources': template.get('resources', {}),
                              'storeData': template.get('storedata', {}),
                              'buildingHealth': template.get('buildinghealthdata', {})})
    if len(templates) != 26:
        raise ValueError('Expected all26 original site templates')
    result = {
        'schema': 1, 'sourceBranch': 'Map Room 3 ordinary overworld',
        'strongholdCombat': {
            'buildingType': 138, 'intactEmitters': emitter_sets[0], 'damagedEmitters': emitter_sets[1],
            'attackRateUnit': 'seconds', 'acquisitionRadiusInclusive': False,
            'retainTargetOutsideRangeAndArc': True, 'healthDamageScale': False,
        },
        'map': {'width': dimensions['WIDTH'], 'height': dimensions['HEIGHT'],
                'sourceMaxPlayers': dimensions['MAX_PLAYERS'], 'minimumHomeDistance': exported(config, 'MIN_PLAYER_DISTANCE'),
                'hexLayout': 'odd-r', 'cellIdFormula': 'y * width + x + 1', 'graphic': hex_size},
        'homeRange': exported(config, 'PLAYER_RANGE'),
        'structureLevels': exported(config, 'STRUCTURE_LEVELS'),
        'structureRanges': exported(config, 'STRUCTURE_RANGE'),
        'resourceProductionRates': exported(config, 'RESOURCE_PRODUCTION_RATES'),
        'resourceCapacities': exported(config, 'RESOURCE_CAPACITIES'),
        'strongholdBonuses': exported(config, 'STRONGHOLD_BONUSES'),
        'fortificationDamageReduction': exported(config, 'DEFENDER_DAMAGE_REDUCTION'),
        'defenderLevels': exported(config, 'DEFENDER_LEVELS'),
        'tribeRegenerationSeconds': exported(config, 'TRIBE_REGEN_TIME') // 1000,
        'attack': {'townHallCostMultipliers': multipliers, 'normalCostMultiplier': 1,
                   'sourceABMultipliers': [1, 0.5, 1.5], 'paidResources': ['twigs', 'pebbles', 'putty'],
                   'formula': 'minimum positive hex distance beyond all owned home/resource/stronghold ranges * townHallMultiplier',
                   'captureDamagePercent': 90, 'capturableTypes': [102, 103, 104],
                   'damageFormula': 'int(100 - 100 * currentNonWallHealth / maximumNonWallHealth)',
                   'worldLevelGap': exported(neighbor, 'WORLD_ATTACK_LEVEL_GAP'),
                   'levelRestrictionExemption': exported(neighbor, 'ATTACK_LEVEL_EXEMPT_FROM'),
                   'lowResourceAttackRestrictedFromPlayerLevel': 32, 'lowResourceMaximumLevel': 20},
        'access': {'minimumTownHallLevel': 6, 'mapRoomBuildingId': 11,
                   'minimumMapRoomHealthFraction': 0.5, 'sourceUIRequiresMR2First': True,
                   'sourceUIBlocksMapRoomUpgradeInProgress': True},
        'siteManagement': {'construct': False, 'upgrade': False, 'fortify': False,
                           'repair': True, 'moveNonImmobileBuildings': True,
                           'repairAllMinimumHealthFraction': 0.25},
        'coreBuildings': core, 'templates': templates,
        'generation': {name: exported(config, name) for name in ('CELL_SEED', 'CELL_EDGE', 'PLACEMENT_NOISE_SCALE',
            'PLACEMENT_THRESHOLD', 'MIN_CELL_ALTITUDE', 'MAX_CELL_ALTITUDE', 'STRONGHOLD_SEED',
            'STRONGHOLD_GRID_SIZE', 'STRONGHOLD_JITTER', 'RESOURCE_SEED', 'TRIBE_OUTPOST_SEED')},
        'references': [{'path': relative, 'sha256': hashlib.sha256((source / relative).read_bytes()).hexdigest()} for relative in REFERENCES],
    }
    return result


def prepare(source: Path, project: Path) -> dict:
    rules = extract(source)
    data = project / 'Plugins/BackyardMMO/Content/Data'
    references = data / 'Original/MapRoom3'
    for entry in rules['references']:
        destination = references / entry['path']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / entry['path'], destination)
    (data / 'maproom-rules.json').write_text(json.dumps(rules, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    (references / 'provenance.json').write_text(json.dumps({'sourceBranch': rules['sourceBranch'], 'files': rules['references']}, indent=2) + '\n', encoding='utf-8')
    return rules


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    rules = prepare(args.source, args.project)
    print(f"Prepared Map Room3: {len(rules['templates'])} authored templates, {len(rules['references'])} source references")


if __name__ == '__main__':
    main()
