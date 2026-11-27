"""Compose the supplied Monster Bunker's door cells without changing source art."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from prepare_combat import _building_atlas


def prepare_bunker(source: Path, assets: Path, data: Path, buildings: list) -> dict:
    bunker = next(building for building in buildings if building['id'] == 22)
    cache = {}
    for level in bunker['levels']:
        animation = _building_atlas(assets, bunker, level, '', cache)
        if not animation or animation['frames'] != 15:
            raise ValueError('The original Bunker strip must contain fifteen complete door cells')
        animation.update(mode='bunker_door', fps=40, loop=False,
                         sourceDisplayHz=40, sourcePhaseMaximum=15,
                         closedFrame=0, openFrame=14,
                         source='client/scripts/BUILDING22.as:TickFast',
                         endpointPolicy='Source phase15 clamps to the final available cell14; no sixteenth cell is invented',
                         stateSource='Authoritative battle.bunkers doorFrame/state/doorAt')
        level['animation'] = animation
    bunker['bunkerPresentation'] = {
        'source': 'client/scripts/BUILDING22.as:TickFast/FindTargets/TickAttack',
        'sound': 'bunkerdoor',
        'doorPhaseMaximum': 15,
        'displayHz': 40,
        'damagedFallback': 'Original static damaged composite below half health; no damaged animation strip exists',
    }
    report = {'schema': 1, 'uniqueAtlases': len(cache), 'buildingLevels': len(bunker['levels']),
              'animation': bunker['levels'][0]['animation'],
              'sourceScriptSha256': hashlib.sha256((source / 'client/scripts/BUILDING22.as').read_bytes()).hexdigest(),
              'originalPixelsModified': False}
    (data / 'bunker-art-provenance.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report
