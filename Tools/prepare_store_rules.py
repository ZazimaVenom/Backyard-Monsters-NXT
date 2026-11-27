#!/usr/bin/env python3
"""Extract the supplied source's worker, mushroom and Shiny shop rules.

This reads literal data and selected source expressions; it never executes AS/TS.
The source runtime's store price overrides the obsolete YARD_PROPS r5 field.
Usage: python Tools/prepare_store_rules.py --source /path/to/backyard-monsters-refitted
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


SOURCES = (
    'client/scripts/QUEUE.as', 'client/scripts/WORKERS.as',
    'client/scripts/WORKER.as', 'client/scripts/MUSHROOMS.as',
    'client/scripts/BMUSHROOM.as', 'client/scripts/BFOUNDATION.as',
    'client/scripts/BUILDING7.as', 'client/scripts/BASE.as',
    'client/scripts/GLOBAL.as', 'client/scripts/STORE.as', 'client/scripts/MAP.as',
    'client/scripts/YARD_PROPS.as', 'client/scripts/BUILDINGBUTTON.as',
    'client/scripts/BUILDINGSPOPUP.as', 'client/scripts/BDECORATION.as',
    'client/scripts/com/gskinner/utils/Rndm.as',
    'server/src/game-data/store/storeItems.ts',
    'server/src/game-data/store/purchaseKeys.ts',
    'server/src/game-data/getDefaultBaseData.ts',
    'server/src/config/GameConfig.ts',
    'server/public/gamestage/assets/english.json',
)


def text(source: Path, relative: str) -> str:
    return (source / relative).read_text(encoding='utf-8-sig')


def number(pattern: str, value: str, description: str) -> float:
    match = re.search(pattern, value, re.S)
    if not match:
        raise ValueError(f'Source rule changed: {description}')
    return float(match.group(1))


def parse_store(source: Path) -> dict:
    result = {}
    raw = text(source, 'server/src/game-data/store/storeItems.ts')
    for key, body in re.findall(r'^\s{2}(\w+):\s*\{([^{}]+)\}', raw, re.M):
        cost = re.search(r'\bc:\s*\[([^\]]*)\]', body)
        title = re.search(r'\bt:\s*("(?:\\.|[^"\\])*")', body)
        if not cost or not title:
            raise ValueError(f'Unsupported store literal: {key}')
        prices = [int(value.strip()) for value in cost.group(1).split(',') if value.strip()]
        result[key] = {'name': json.loads(title.group(1)), 'costs': prices}
    if not all(key in result for key in ('BEW', 'ENL', 'BIP', 'SP1', 'SP2', 'SP3', 'SP4')):
        raise ValueError('The original worker/expansion items were not found')
    return result


def parse_yard(source: Path) -> list:
    # Reuse the existing non-executing literal parser, imported lazily so the
    # main asset pipeline can pass its already parsed original YARD_PROPS rows.
    from prepare_assets import LiteralParser
    raw = text(source, 'client/scripts/YARD_PROPS.as')
    marker = re.search(r'_[A-Za-z_]+\s*:\s*Array\s*=\s*', raw)
    if not marker:
        raise ValueError('Original YARD_PROPS literal not found')
    return LiteralParser(raw[marker.end():]).value()


def build_rules(source: Path, assets: Path, buildings: list, raw_yard: list | None = None) -> dict:
    store = parse_store(source)
    yard = raw_yard if raw_yard is not None else parse_yard(source)
    catalog = {int(row['id']): row for row in buildings}
    english = json.loads(text(source, 'server/public/gamestage/assets/english.json'))
    if 'core' in english:
        english = english['core']
    popup = text(source, 'client/scripts/BUILDINGSPOPUP.as')
    category_match = re.search(r'if \(param1 == 4\)\s*\{\s*this.SubMenu\(\[(.*?)\]\)', popup, re.S)
    if not category_match:
        raise ValueError('Original decoration submenu not found')
    category_keys = re.findall(r'KEYS.Get\("([^"]+)"\)', category_match.group(1))
    categories = [english[key] for key in category_keys]
    decorations, excluded = [], []
    for row in yard:
        if row.get('type') != 'decoration':
            continue
        identity = int(row['id'])
        product = store.get(f'BUILDING{identity}')
        reason = ('blocked in original catalog; not offered by the normal shop' if row.get('block') else
                  'no store purchase item' if not product else
                  'free promotional or reward item' if not product['costs'] or product['costs'][0] <= 0 else '')
        if reason:
            excluded.append({'type': identity, 'reason': reason})
            continue
        building = catalog.get(identity)
        if not building:
            raise ValueError(f'Priced decoration {identity} missing from the prepared building catalog')
        candidates = [building.get('menuTexture'), building.get('thumbnailTexture'),
                      building.get('levels', [{}])[0].get('compositeTexture'), building.get('texture')]
        texture = next((name for name in candidates if name and (assets / name).is_file()), None)
        if not texture:
            raise ValueError(f'Priced decoration {identity} has no retained artwork')
        if row['costs'][0]['time'] != 0:
            raise ValueError(f'Unexpected timed decoration {identity}')
        subgroup = int(row['subgroup'])
        decorations.append({
            'type': identity, 'price': product['costs'][0], 'name': building['name'],
            'texture': texture, 'limit': int(row['quantity'][0]),
            'category': categories[subgroup], 'categoryKey': category_keys[subgroup],
            'subgroup': subgroup, 'order': int(row.get('order', 0)),
            'sourceCatalogR5': row['costs'][0].get('r5', 0),
            'sourceStoreItem': f'BUILDING{identity}',
            'sourceFootprintPixels': int(row['size']),
        })

    queue = text(source, 'client/scripts/QUEUE.as')
    original_store = text(source, 'client/scripts/STORE.as')
    mushrooms = text(source, 'client/scripts/MUSHROOMS.as')
    mushroom_actor = text(source, 'client/scripts/BMUSHROOM.as')
    global_source = text(source, 'client/scripts/GLOBAL.as')
    account = text(source, 'server/src/game-data/getDefaultBaseData.ts')
    config = text(source, 'server/src/config/GameConfig.ts')
    initial_fallback = int(number(r'credits:\s*devConfig.shiny\s*\|\|\s*(\d+)', account, 'initial Shiny fallback'))
    configured_shiny = int(number(r'\bshiny:\s*(\d+)', config, 'configured initial Shiny'))
    default_workers = int(number(r'if \(param1 == 0\)\s*\{\s*param1 = (\d+)', queue, 'initial worker count'))
    maximum_workers = int(number(r'QUEUE\._workerCount >= (\d+)', original_store, 'worker purchase limit'))
    purchase = original_store.split('public static function ProcessPurchases()', 1)[1]
    width = int(number(r'GLOBAL\._mapWidth = (\d+)', purchase, 'source yard width'))
    height = int(number(r'GLOBAL\._mapHeight = (\d+)', purchase, 'source yard height'))
    factor = number(r'GLOBAL\._mapWidth \*= ([\d.]+)', purchase, 'expansion multiplier')
    quantum = int(number(r'GLOBAL\._mapWidth = Math.ceil\(GLOBAL\._mapWidth / (\d+)\)', purchase, 'expansion rounding'))
    packing_increment = number(r'GLOBAL\._upgradePacking \+= ([\d.]+) \* _storeData.BIP.q', purchase, 'packing capacity increment')
    free_seconds = int(number(r'if \(param2 && param1 <= (\d+)\)', original_store, 'free finish threshold'))
    speedup_rules = {
        'SP1': {'price': store['SP1']['costs'][0], 'seconds': free_seconds, 'minimumRemainingExclusive': 0,
                'maximumRemainingInclusive': free_seconds, 'finishesJob': True},
        'SP2': {'price': store['SP2']['costs'][0], 'seconds': 3600, 'minimumRemainingInclusive': 3600},
        'SP3': {'price': store['SP3']['costs'][0], 'seconds': 7200, 'minimumRemainingInclusive': 7200},
        'SP4': {'minimumRemainingExclusive': free_seconds, 'finishesJob': True,
                'priceFormula': 'min(ceil(int(remainingSeconds) * 20 / 3600), int(sqrt(int(remainingSeconds) * 0.8)))'},
        'sourcePriority': ['repair', 'construction', 'upgrade', 'fortification', 'locker unlock', 'academy research', 'monster lab ability'],
        'completedWorkBypassesWorkerArrival': True,
        'inventoryException': 'Owned SP2/SP3 inventory may be consumed for any remaining time above 300 seconds; normal Shiny purchases require the full reduction duration',
        'sourceRepairReduction': 'ceil(maxHealth / repairTimeRemaining) * reductionSeconds; SP1/SP4 finish repair',
        'boundaryChoice': 'Use CalcCost free threshold <=300; the streamlined popup has an inconsistent <300 selector at exactly 300',
    }
    for source_expression in ('_loc12_ < 60 * 60 && !_storeInventory.SP2',
                              '_loc12_ < 60 * 60 * 2 && !_storeInventory.SP3',
                              '_loc16_ = 60 * 60;', '_loc16_ = 60 * 60 * 2;',
                              '_loc3_ = Math.ceil(param1 * 20 / 60 / 60)',
                              '_loc4_ = int(Math.sqrt(param1 * 0.8))'):
        if source_expression not in original_store:
            raise ValueError(f'Speedup source expression changed: {source_expression}')
    dimensions = []
    for level in range(len(store['ENL']['costs']) + 1):
        w, h = width, height
        for _ in range(level):
            w, h = int(w * factor), int(h * factor)
        dimensions.append([((w + quantum - 1) // quantum) * quantum,
                           ((h + quantum - 1) // quantum) * quantum])
    clusters = int(number(r'while \(i < (\d+)\)', mushrooms, 'initial cluster loop')) - 1
    per_cluster = int(number(r'spawn = (\d+)', mushrooms, 'initial mushrooms per cluster'))
    interval = int(number(r'spawnCount = Math.floor\([^\n]+\) / (\d+)', mushrooms, 'regrowth interval'))
    saved_cap = int(number(r'Math.min\(BASE\._mushroomList.length, (\d+)\)', mushrooms, 'retained mushroom cap'))
    catchup_cap = int(number(r'if \(spawnCount > (\d+)\)\s*\{\s*spawnCount = \1;', mushrooms, 'regrowth catchup cap'))
    growth_cap = int(number(r'if \(mushroomCount \+ spawnCount > (\d+)\)', mushrooms, 'regrowth population cap'))
    attempts = int(number(r'_loc8_ < (\d+)', mushrooms, 'spawn attempt limit'))
    margin = int(number(r'_loc6_ = (\d+) \+ GLOBAL\._mapWidth', mushrooms, 'spawn margin'))
    variants = int(number(r'_loc4_ = int\(Math.random\(\) \* (\d+)\) \+ 1', mushrooms, 'visual mushroom variants'))
    shake_threshold = int(number(r'if \(_shake > (\d+)', mushroom_actor, 'mushroom shake threshold'))
    display_hz = int(number(r'_fps = (\d+)', global_source, 'original display cadence'))
    golden_denominator = int(number(r'isGolden:Boolean = int\(positionRng.random\(\) \* (\d+)\)', mushrooms, 'golden mushroom probability'))
    golden_variants = int(number(r'mushroomVariant:int = int\(Math.random\(\) \* (\d+) \+ 1\)', mushrooms, 'golden reward variant roll'))
    rewards = re.search(r'shinyAwarded = mushroomVariant == 2 \? (\d+) : (\d+)', mushrooms)
    if not rewards or golden_variants != 3 or 'if (mushroomVariant == 3)' not in mushrooms:
        raise ValueError('Golden mushroom reward branch changed')
    high, low = map(int, rewards.groups())
    footprint = int(number(r'_footprint = \[new Rectangle\(0, 0, (\d+),',
                           text(source, 'client/scripts/BUILDING7.as'), 'mushroom footprint'))
    rules = {
        'schema': 1,
        'sourceBranch': 'Supplied backyard-monsters-refitted ordinary overworld main yard; no claim of historical launch-era economy',
        'initialShiny': configured_shiny or initial_fallback,
        'initialShinyConfigured': configured_shiny,
        'initialShinyUnsetFallback': initial_fallback,
        'workerCosts': store['BEW']['costs'],
        'initialWorkers': default_workers, 'maxWorkers': maximum_workers,
        'outpostWorkers': 1,
        'workerRules': {'queueIsPendingFIFO': False, 'buildUpgradeFortifyRequireWorker': True,
                        'zeroTimeConstructionRequiresWorker': False, 'repairRequiresWorker': False,
                        'damagedPaidJobRetainsWorker': True,
                        'acquisitionItem': 'BEW', 'permanent': True},
        'expansionCosts': store['ENL']['costs'],
        'packingCosts': store['BIP']['costs'],
        'packingRules': {'item': 'BIP', 'maxLevel': len(store['BIP']['costs']),
                         'capacityIncrementPerLevel': packing_increment, 'compound': False,
                         'capacityFormula': 'floor(baseCapacity * (1 + 0.1 * purchasedLevel))',
                         'appliesToAllFourResources': True, 'permanent': True},
        'speedupRules': speedup_rules,
        'expansionSourceDimensions': dimensions,
        'expansionRules': {'item': 'ENL', 'baseWidth': width, 'baseHeight': height,
                           'dimensionMultiplier': factor, 'roundingPixels': quantum,
                           'integerTruncationEachStep': True, 'roundAfterAllSteps': True,
                           'permanent': True, 'infernoItemIncluded': False,
                           'sourceBoundary': 'MAP.Edge: closed white two-pixel line at 50 percent alpha, GRID.ToISO of centered +/- width/2 and height/2',
                           'gridTranslationNote': 'NXT integer-cell bounds retain the source extents; centering an odd cell count may translate the rectangle by half a cell'},
        'mushroomRules': {
            'initial': clusters * per_cluster, 'initialClusters': clusters,
            'initialPerCluster': per_cluster, 'retainedCap': saved_cap,
            'regrowthCap': growth_cap, 'interval': interval, 'maxCatchup': catchup_cap,
            'spawnMarginPixels': margin, 'spawnAttempts': attempts,
            'footprintPixels': footprint, 'visualVariants': variants,
            'pickCalls': shake_threshold + 2, 'displayHz': display_hz,
            'pickSeconds': (shake_threshold + 2) / display_hz,
            'workerRequired': True, 'resourceCost': 0, 'shinyCost': 0,
            'goldenChanceNumerator': 1, 'goldenChanceDenominator': golden_denominator,
            'goldenRewards': [{'shiny': low, 'weight': 2}, {'shiny': high, 'weight': 1}],
            'ordinaryReward': 0, 'visualVariantDeterminesReward': False,
            'sourceGrowthTrigger': 'MUSHROOMS.Setup on yard load; reset timestamp to current time after an elapsed interval even if population is full',
            'sourceGoldenRng': 'Rndm(uint(int(mushroom.x * mushroom.y))); Flash BitmapData.noise; floor(random()*4)==0',
            'serverAdaptation': 'Authoritative unique mushroom identities and persisted private rewards; never accept a client reward or replay a consumed identity',
        },
        'decorationCategories': categories,
        'decorations': decorations, 'excludedDecorations': excluded,
        'decorationRules': {'priceSource': 'storeItems BUILDING<type>.c[0]; BUILDINGBUTTON reads this instead of YARD_PROPS r5',
                            'zeroLimitMeansUnlimited': True, 'constructionSeconds': 0,
                            'workerRequired': False, 'recycleReturnsToStorage': True,
                            'storagePlacementChargesShiny': False},
        'provenance': 'Original/Store/provenance.json',
    }
    return rules


def prepare_store_rules(source: Path, assets: Path, data: Path, buildings: list,
                        raw_yard: list | None = None) -> dict:
    rules = build_rules(source, assets, buildings, raw_yard)
    retained = data / 'Original/Store'
    provenance = {'schema': 1, 'sources': {}}
    for relative in SOURCES:
        original = source / relative
        target = retained / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        contents = original.read_bytes()
        provenance['sources'][relative] = {'file': relative, 'bytes': len(contents),
                                          'sha256': hashlib.sha256(contents).hexdigest()}
    data.mkdir(parents=True, exist_ok=True)
    (retained / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    (data / 'store-rules.json').write_text(json.dumps(rules, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    return rules


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    plugin = args.project / 'Plugins/BackyardMMO'
    data = plugin / 'Content/Data'
    catalog = json.loads((data / 'catalog.json').read_text())
    rules = prepare_store_rules(args.source, plugin / 'SourceAssets', data, catalog['buildings'])
    print(f"Prepared {len(rules['decorations'])} original priced decorations, worker/mushroom/expansion rules")


if __name__ == '__main__':
    main()
