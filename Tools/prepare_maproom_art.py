#!/usr/bin/env python3
"""Extract Map Room 3 presentation metadata from retained original source.

No map art is repainted or resized. All image paths resolve to the original
worldmap assets already included in SourceAssets.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import re
import shutil
from pathlib import Path
from PIL import Image

SOURCE_FILES = (
    'client/scripts/com/monsters/maproom3/MapRoom3AssetCache.as',
    'client/scripts/com/monsters/maproom3/MapRoom3CellGraphic.as',
    'client/scripts/com/monsters/maproom3/MapRoom3Window.as',
    'client/scripts/com/monsters/maproom3/MapRoom3WindowHUD.as',
    'client/scripts/com/monsters/maproom3/tiles/MapRoom3TileSetManager.as',
    'client/scripts/com/monsters/maproom3/tiles/MapRoom3TileSet.as',
    'client/scripts/com/monsters/maproom3/tiles/MapRoom3TileSetRange.as',
)

ICON_SYMBOLS = {
    'playerBase': 'CELL_ICON_PLAYER_BASE',
    'resourceCell': 'CELL_ICON_RESOURCE_CELL',
    'stronghold': 'CELL_ICON_STRONGHOLD',
    'wildMonsterBase': 'CELL_ICON_WILD_MONSTER_BASE',
    'damageProtection': 'CELL_ICON_DAMAGE_PROTECTION',
    'fortification': 'CELL_ICON_FORTIFICATION',
    'fortificationEast': 'CELL_ICON_FORTIFICATION_EAST',
    'fortificationWest': 'CELL_ICON_FORTIFICATION_WEST',
    'fortificationNorthEast': 'CELL_ICON_FORTIFICATION_NORTH_EAST',
    'fortificationNorthWest': 'CELL_ICON_FORTIFICATION_NORTH_WEST',
    'fortificationSouthEast': 'CELL_ICON_FORTIFICATION_SOUTH_EAST',
    'fortificationSouthWest': 'CELL_ICON_FORTIFICATION_SOUTH_WEST',
    'fullyFortifiedBack': 'CELL_ICON_FULLY_FORTIFIED_BACK',
    'fullyFortifiedFront': 'CELL_ICON_FULLY_FORTIFIED_FRONT',
}

HUD_SYMBOLS = {
    'zoomIn': 'HUD_BUTTON_ZOOM_IN', 'zoomOut': 'HUD_BUTTON_ZOOM_OUT',
    'fullScreen': 'HUD_BUTTON_FULL_SCREEN', 'buttonsBackground': 'HUD_BUTTONS_BAR_BACKGROUND',
    'coordinatesBackground': 'HUD_COORDINATES_BACKGROUND',
    'bookmarkResource': 'HUD_BOOKMARK_THUMBNAIL_RESOURCE',
    'bookmarkStronghold': 'HUD_BOOKMARK_THUMBNAIL_STRONGHOLD',
    'mouseoverBackground': 'MOUSEOVER_BACKGROUND',
    'buttonBackground': 'MOUSEOVER_BUTTON_BACKGROUND',
    'enter': 'MOUSEOVER_BUTTON_ENTER', 'enterHover': 'MOUSEOVER_BUTTON_ENTER_ROLLOVER',
    'scoutAttack': 'MOUSEOVER_BUTTON_SCOUT_ATTACK', 'scoutAttackHover': 'MOUSEOVER_BUTTON_SCOUT_ATTACK_ROLLOVER',
    'bookmarkAdd': 'MOUSEOVER_BUTTON_BOOKMARK_ADD', 'bookmarkAddHover': 'MOUSEOVER_BUTTON_BOOKMARK_ADD_ROLLOVER',
    'bookmarkRemove': 'MOUSEOVER_BUTTON_BOOKMARK_REMOVE', 'bookmarkRemoveHover': 'MOUSEOVER_BUTTON_BOOKMARK_REMOVE_ROLLOVER',
}


def source_number(text: str, name: str) -> int:
    match = re.search(r'const\s+' + re.escape(name) + r':(?:int|uint|Number)\s*=\s*(-?\d+)\s*;', text)
    if not match:
        raise ValueError(f'Missing numeric source constant {name}')
    return int(match.group(1))


def flash_hash(cell_index: int) -> int:
    """Exact AS3 bitwise conversions, including >>>35 wrapping to >>>3."""
    value = cell_index & 0xffffffff
    value = (value ^ (value << 21)) & 0xffffffff
    value = (value ^ (value >> (35 & 31))) & 0xffffffff
    value = (value ^ (value << 4)) & 0xffffffff
    signed = value if value < 0x80000000 else value - 0x100000000
    return abs(signed)


def select_tile(art: dict, altitude: int, cell_index: int):
    ranges = art['altitudeRanges']
    if not ranges or altitude < ranges[0]['start']:
        return None
    for item in ranges:
        if item['end'] >= altitude:
            options = item['tileIndices']
            return art['tiles'][options[flash_hash(cell_index) % len(options)]] if options else None
    return None


def build_maproom_art(source: Path, assets: Path) -> dict:
    texts = {name: (source / name).read_text(encoding='utf-8') for name in SOURCE_FILES}
    cache = texts[SOURCE_FILES[0]]
    graphic = texts[SOURCE_FILES[1]]
    manager = texts[SOURCE_FILES[4]]
    symbols = dict(re.findall(r'const\s+(\w+):String\s*=\s*"([^"]+)"\s*;', cache))
    hex_width = source_number(graphic, 'HEX_WIDTH')
    hex_height = source_number(graphic, 'HEX_HEIGHT')
    row_stride = source_number(graphic, 'HEX_HEIGHT_OVERLAP')
    border = source_number(manager, 'BORDER_CELL_HEIGHT')
    block = manager.split('DEFAULT_TILE_SET:Array =', 1)[1].split(';', 1)[0]
    block = re.sub(r'BORDER_CELL_HEIGHT\s*-\s*1', str(border - 1), block)
    block = block.replace('BORDER_CELL_HEIGHT', str(border))
    raw_tiles = json.loads(block)

    inputs = {}
    def image(path: str, anchor: str = 'none') -> dict:
        raw = (assets / path).read_bytes()
        with Image.open(assets / path) as im:
            width, height, mode = im.width, im.height, im.mode
        digest = hashlib.sha256(raw).hexdigest()
        inputs[path] = {'sha256': digest, 'bytes': len(raw), 'width': width, 'height': height, 'mode': mode}
        result = {'path': path, 'imageWidth': width, 'imageHeight': height}
        if anchor == 'tile':
            result.update(offsetX=(hex_width-width)*.5,
                          offsetY=hex_height-height if height>hex_height else (hex_height-height)*.5)
        elif anchor == 'icon':
            # ActionScript Math.round rounds a negative half toward +infinity.
            result.update(offsetX=math.floor((hex_width-width)*.5+.5),
                          offsetY=math.floor((hex_height-height)*.5+.5))
        return result

    tiles = []
    for tile in raw_tiles:
        entry = image(tile['src'], 'tile')
        entry.update(minAltitude=tile['min_alt'], maxAltitude=tile['max_alt'])
        tiles.append(entry)
    cutoffs = sorted({height for tile in raw_tiles for height in (tile['min_alt'], tile['max_alt'])})
    ranges = [{'start': a, 'end': b,
               'tileIndices': [i for i, tile in enumerate(raw_tiles) if a >= tile['min_alt'] and b <= tile['max_alt']]}
              for a, b in zip(cutoffs, cutoffs[1:])]
    icons = {key: image(symbols[symbol], 'icon') for key, symbol in ICON_SYMBOLS.items()}
    overlays = {}
    lights = {}
    for relation, color in [('self', 'BLUE'), ('ally', 'GREEN'), ('neutral', 'YELLOW'), ('enemy', 'RED')]:
        overlays[relation] = image(symbols['CELL_OVERLAY_GLOW_'+color])
        lights[relation] = image(symbols['CELL_ICON_FORTIFICATION_LIGHT_'+color], 'icon')
    frames = source_number(cache, 'STRONGHOLD_BUFF_EFFECT_TOTAL_FRAMES')
    effects = {}
    for relation in ['neutral', 'enemy', 'player', 'mixed']:
        item = image(symbols['CELL_ICON_STRONGHOLD_BUFF_EFFECT_'+relation.upper()])
        if item['imageWidth'] % frames:
            raise ValueError('Source stronghold sheet is not divisible into forty cells')
        frame_width = item['imageWidth'] // frames
        item.update(frames=frames, frameWidth=frame_width, frameHeight=item['imageHeight'],
                    offsetX=math.floor((hex_width-frame_width)*.5+.5),
                    offsetY=math.floor((hex_height-item['imageHeight'])*.5+.5)+source_number(cache, 'STRONGHOLD_BUFF_EFFECT_OFFSET_Y'))
        effects[relation] = item
    background = re.search(r'DEFAULT_BACKGROUND:String\s*=\s*"([^"]+)"', manager).group(1)
    hud = {key: image(symbols[symbol]) for key, symbol in HUD_SYMBOLS.items()}
    health = image(symbols['DAMAGE_BAR'])
    health.update(frameWidth=source_number(cache, 'DAMAGE_BAR_WIDTH'),
                  frameHeight=source_number(cache, 'DAMAGE_BAR_SEGMENT_HEIGHT'),
                  frames=source_number(cache, 'DAMAGE_BAR_TOTAL_HEIGHT')//source_number(cache, 'DAMAGE_BAR_SEGMENT_HEIGHT'))
    return {'schema': 1, 'branch': 'MapRoom3', 'originalPixelsModified': False,
            'geometry': {'hexWidth': hex_width, 'hexHeight': hex_height, 'rowStride': row_stride,
                         'oddRowOffset': hex_width*.5, 'edgeLength': source_number(graphic, 'HEX_EDGE_LENGTH'),
                         'hexVertices': [[52,0],[104,17],[104,50],[52,68],[0,50],[0,17]]},
            'background': image(background), 'tiles': tiles, 'altitudeRanges': ranges,
            'blockedAltitude': source_number(manager, 'BLOCKED_CELL_STARTING_HEIGHT'),
            'borderAltitude': border, 'icons': icons, 'overlays': overlays,
            'fortificationLights': lights, 'strongholdEffects': effects, 'damageBar': health, 'hud': hud,
            'selection': {'rangeRule': 'Require height >= first start; use first range with end >= height; empty options means no plant tile.',
                          'index': 'cellY * mapWidth + cellX',
                          'hash': 'uint32 v=index; v^=v<<21; v^=v>>3; v^=v<<4; abs(int64(int32(v))) % optionCount',
                          'notes': 'AS3 shift counts wrap modulo32; >>>35 is >>>3. Preserve overlapping/spare source ranges rather than rounding altitude to a made-up tile.'},
            'inputs': dict(sorted(inputs.items())),
            'sourceReferences': [{'path': name, 'sha256': hashlib.sha256((source/name).read_bytes()).hexdigest()}
                                 for name in SOURCE_FILES],
            'excluded': ['Inferno-only worldmap/tiles/tests/lava.png is absent from the supplied assets and is not used by ordinary Map Room 3.',
                         'Hellraiser event icons and alliance/truce/message controls remain preserved source assets; this metadata does not claim their gameplay is implemented.']}


def prepare_maproom_art(source: Path, assets: Path, data: Path) -> dict:
    retained = data / 'Original/MapRoom3Art'
    for relative in SOURCE_FILES:
        destination = retained / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source/relative, destination)
    report = build_maproom_art(retained, assets)
    (data/'maproom-art.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    plugin = args.project/'Plugins/BackyardMMO'
    report = prepare_maproom_art(args.source, plugin/'SourceAssets', plugin/'Content/Data')
    print(json.dumps({'originalImages': len(report['inputs']), 'terrainTiles': len(report['tiles']),
                      'sourceReferences': len(report['sourceReferences']), 'modifiedImages': 0}))


if __name__ == '__main__':
    main()
