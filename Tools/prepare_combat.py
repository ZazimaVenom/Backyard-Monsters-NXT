"""Source-audited combat presentation metadata and original-pixel atlas composition.

Facing columns are never relabelled as animation frames. The early monster
strips contain one pose per direction; later source branches select explicit
rows. This module records that distinction, including absent death sequences.
"""
from __future__ import annotations

import hashlib
import json
import math
import re
import shutil
from pathlib import Path

from PIL import Image, ImageOps


SPRITE_SOURCE = 'client/scripts/SPRITES.as:GetSprite'
MONSTER_SOURCE = 'client/scripts/com/monsters/monsters/MonsterBase.as'
CREEP_SOURCE = 'client/scripts/com/monsters/monsters/creeps/CreepBase.as'


def source_timing(source: Path) -> dict:
    code = (source / 'client/scripts/GLOBAL.as').read_text(encoding='utf-8-sig')
    match = re.search(r'SIM_STEPS_PER_MS:Number\s*=\s*([\d.]+)', code)
    if not match:
        raise ValueError('Original simulation tick rate is missing; do not guess animation timing')
    display = json.loads((source / 'asconfig.json').read_text(encoding='utf-8-sig'))
    hz = display['compilerOptions']['default-frame-rate']
    return {'simulationHz': float(match.group(1)) * 1000, 'displayHz': hz,
            'simulationSource': 'client/scripts/GLOBAL.as:SIM_STEPS_PER_MS/TickFast',
            'displaySource': 'asconfig.json:compilerOptions.default-frame-rate'}


def sprite_animations(sprite_id: str, descriptor: dict, timing: dict) -> dict:
    """Translate every SPRITES.GetSprite row branch without inventing animation."""
    count = descriptor['rows']

    def sequence(rows, ticks=0, source_state='walking', loop=True, availability=None):
        requested = list(rows)
        available = [n for n in requested if 0 <= n < count]
        result = {'rows': available, 'fps': timing['simulationHz'] / ticks if ticks else 0,
                  'ticksPerFrame': ticks, 'loop': bool(loop and ticks and len(available) > 1),
                  'sourceState': source_state, 'source': SPRITE_SOURCE,
                  'availability': availability or ('source_frames' if len(available) > 1 else 'directional_only' if available else 'unavailable_source_frames')}
        if available != requested:
            result['unavailableSourceRows'] = sorted(set(requested) - set(available))
        return result

    default_row = 1 if sprite_id == 'C19' else 0
    result = {name: sequence([default_row]) for name in ('idle', 'walk', 'attack')}
    result['death'] = sequence([], loop=False, source_state='', availability='no_source_sequence')
    result['death']['source'] = MONSTER_SOURCE + ':die/dieFinish/deathSplat'
    if sprite_id in ('C14', 'C16'):
        for state in ('idle', 'walk', 'attack'):
            result[state] = sequence(range(3), 3, 'flying')
        result['death'] = sequence([0], source_state='landed', loop=False)
    elif sprite_id in ('IC1', 'IC3', 'IC5'):
        frames = {'IC1': 2, 'IC3': 8, 'IC5': 6}[sprite_id]
        for state in ('idle', 'walk', 'attack'):
            result[state] = sequence(range(1, frames + 1), 8)
    elif sprite_id == 'C19':
        result['idle'] = sequence([1], source_state='idle')
        result['walk'] = sequence(range(1, 6), 8, 'moving')
        result['attack'] = sequence([1], source_state='idle')
    elif sprite_id == 'C9':
        result['invisible'] = sequence([1], source_state='invisible')
    elif sprite_id == 'C13':
        result['burrowed'] = sequence([4], source_state='burrowed')
        result['transition'] = sequence(range(5), source_state='transition', loop=False)
        result['transition']['selection'] = 'explicit source frame parameter, not a timed loop'
    elif sprite_id in ('worker', 'inferno_worker'):
        result['boosted'] = sequence([1], source_state='STORE._storeData.BST')
    elif sprite_id == 'C200':
        for state in ('idle', 'walk', 'attack'):
            result[state] = sequence([1], source_state='not empty')
        result['empty'] = sequence([0], source_state='empty')
        result['loaded'] = sequence([1], source_state='not empty')
    elif sprite_id.startswith(('G1_', 'G2_')):
        level = int(sprite_id.split('_')[1])
        result['idle'] = sequence([0], source_state='idle')
        result['walk'] = sequence(range(1, 8), 8)
        result['attack'] = sequence(range(8, 16 if level >= 4 else 15), 8, 'attack')
    elif sprite_id.startswith('G3_'):
        level = int(sprite_id.split('_')[1])
        frames = 7 if level == 1 else 8 if level == 2 else 6
        result['idle'] = sequence([0], source_state='idle')
        result['walk'] = sequence(range(1, frames + 1), 8)
        result['attack'] = sequence(range(1, frames + 1), 8, 'attack')
    elif sprite_id.startswith('G4_'):
        level = int(sprite_id.split('_')[1])
        walk_frames = 8 if level <= 2 else 9 if level == 3 else 10
        attack_frames = 9 if level <= 2 else 10
        result['idle'] = sequence([0], source_state='idle')
        result['walk'] = sequence(range(walk_frames), 8)
        result['attack'] = sequence(range(walk_frames, walk_frames + attack_frames), 8, 'attack')
        result['stomp'] = sequence(range(20, 30), 8, 'stomp')
    elif sprite_id.startswith('G5_'):
        result['idle'] = sequence(range(10), 8, 'idle')
        result['walk'] = sequence(range(10), 8)
        result['attack'] = sequence(range(10, 16), 8, 'attack')
    return result


def normalized_footprint(building: dict) -> dict | None:
    """GRID.Block projects only rectangle origins; width/height are world axes."""
    raw = building.get('collisionFootprints')
    if not raw:
        return None
    rects = [[r[1] + r[0] * .5, r[1] - r[0] * .5, r[2], r[3]] for r in raw]
    x0, y0 = min(r[0] for r in rects), min(r[1] for r in rects)
    x1, y1 = max(r[0] + r[2] for r in rects), max(r[1] + r[3] for r in rects)
    return {'units': 'source_cartesian_pixels', 'minX': x0, 'minY': y0,
            'maxX': x1, 'maxY': y1, 'centerX': (x0 + x1) / 2, 'centerY': (y0 + y1) / 2,
            'width': x1 - x0, 'height': y1 - y0, 'rectangles': rects,
            'rawMixedCoordinateRectangles': raw, 'pixelsPerGridCell': 20,
            'pivot': {'x': 0, 'y': 0},
            'source': building.get('classSource', '') + ':_footprint; client/scripts/GRID.as:Block/FromISO',
            'originTransform': 'u=y+x/2; v=y-x/2; dimensions stay Cartesian; art anchor is (0,0)'}


def prepare_effects(source: Path, assets: Path, timing: dict, sprites: dict) -> dict:
    """Keep source MovieClip poses distinct from temporal animation sequences."""
    effects = {}

    def shape(key, shape_id, pivot, symbol, source_frame=1, scale=1):
        src = source / 'client/shapes' / f'{shape_id}.png'
        relative = f'derived/effects/original/{key}.png'
        out = assets / relative
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)
        with Image.open(src) as image:
            width, height = image.size
        effects[key] = {'texture': relative, 'frameWidth': width, 'frameHeight': height,
            'pivotX': pivot[0], 'pivotY': pivot[1], 'columns': 1, 'rows': 1,
            'frames': 1, 'fps': 0, 'loop': False, 'sourceScale': scale,
            'source': f'client/scripts/_assets/assets.swf:{symbol} frame {source_frame}; shape {shape_id}',
            'sourceShape': shape_id, 'sourceFrame': source_frame,
            'sha256': hashlib.sha256(src.read_bytes()).hexdigest(),
            'animationMode': 'translated_source_pose'}

    shape('cannonball', 1836, (4, 4), 'PROJECTILE_CLIP')
    effects['cannonball']['shadow'] = {'sourceShape': 1836, 'scaleY': .75, 'offsetX': -4, 'offsetY': 17}
    effects['bullet'] = dict(effects['cannonball'])
    shape('splat', 1818, (7.6, 4.05), 'ParticleSplat', 5)
    effects['splat'].update(animationMode='source_static_variant', duration=67 / timing['displayHz'],
        durationRange=[11 / timing['displayHz'], 67 / timing['displayHz']],
        durationStatus='Maximum moving-particle lifetime from source speed<20/15, deceleration .02/displayframe; the source then snapshots a persistent mark',
        sourceVariantCount=5, preparedSourceVariants=[5],
        limitation='Source variants1..4 are morph shape1817 at ratios0,16384,32768,49152; only the intact frame5 shape is selected, never relabelled as an animation')
    shape('giblet', 1707, (2.55, 2.8), 'GIBLET_CLIP')
    shape('fireball', 2547, (4, 4), 'FIREBALL_CLIP', 1, 2)
    effects['fireball']['glow'] = {'rgb': 16748544, 'blurX': 12, 'blurY': 12, 'strength': 6}
    effects['fireball']['speedPixelsPerTick'] = 6
    shape('healball', 1563, (4.5, 4.5), 'FIREBALL_CLIP', 2, 2)
    effects['healball']['speedPixelsPerTick'] = 25
    smoke_source = source / 'client/images/1565_smoke1_smoke1.png'
    smoke_relative = 'derived/effects/original/smoke.png'
    shutil.copy2(smoke_source, assets / smoke_relative)
    with Image.open(smoke_source) as image:
        if image.size != (3000, 30):
            raise ValueError('Original Smoke.Setup sprite strip must be3000x30')
    effects['smoke'] = {'texture': smoke_relative, 'frameWidth': 30, 'frameHeight': 30,
        'pivotX': 15, 'pivotY': 15, 'columns': 100, 'rows': 1, 'frames': 100,
        'fps': 0, 'loop': False, 'sourceScale': 2, 'animationMode': 'particle_life_selects_frame',
        'source': 'client/scripts/com/monsters/effects/smoke/Smoke.as:Setup/Tick',
        'sourceFrameCrop': 'width=int(5+25*frame/100); offset=int((30-width)/2)',
        'sha256': hashlib.sha256(smoke_source.read_bytes()).hexdigest()}
    if 'rocket' in sprites:
        effects['rocket'] = {k: sprites['rocket'][k] for k in ('texture','frameWidth','frameHeight','pivotX','pivotY','columns','rows','frames','directions')}
        effects['rocket'].update(fps=0, loop=False, animationMode='directional_pose',
            source='client/scripts/com/monsters/monsters/creeps/rebalance/DAVEv2.as:rangedAttack; SPRITES.as:rocket',
            availability='powered_up_DAVE_only')
    line = 'derived/effects/source_line.png'
    Image.new('RGBA', (1, 1), (255, 255, 255, 255)).save(assets / line)
    effects['laser'] = {'texture': line, 'frameWidth': 1, 'frameHeight': 1,
        'pivotX': .5, 'pivotY': .5, 'columns': 1, 'rows': 1, 'frames': 1, 'fps': 0,
        'loop': False, 'animationMode': 'source_vector_beam', 'rgb': 16051677,
        'lineWidthMinPixels': 2, 'lineWidthMaxPixels': 4, 'glowRGB': 13313803,
        'glowBlurPixels': 20, 'hitRGB': 16555315, 'hitEllipsePixels': [16, 8],
        'source': 'client/scripts/com/monsters/effects/LASER.as:Tick',
        'duration': 100 / timing['simulationHz']}
    effects['lightning'] = {'texture': line, 'frameWidth': 1, 'frameHeight': 1,
        'pivotX': .5, 'pivotY': .5, 'columns': 1, 'rows': 1, 'frames': 1, 'fps': 0,
        'loop': False, 'animationMode': 'source_vector_bolt', 'rgb': 3197178,
        'lineWidthPixels': 1, 'segmentPixels': 30, 'jitterMinimumPixels': -7,
        'jitterRangePixels': 15, 'glowBlurPixels': 4, 'alphaDivisorPerFrame': 1.75,
        'source': 'client/scripts/EFFECTS.as:Lightning/Tick', 'duration': 3 / timing['displayHz']}
    for key in ('cannonball', 'bullet', 'fireball', 'healball', 'rocket'):
        if key in effects:
            effects[key]['sourceSimulationHz'] = timing['simulationHz']
    # Normalized UVs use the complete image, including trailing source padding.
    # Frame counts cannot recover this for e.g. IC6 (1718px, 30*57=1710px).
    for effect in effects.values():
        with Image.open(assets / effect['texture']) as image:
            effect.update(textureWidth=image.width, textureHeight=image.height)
    return effects


def _building_atlas(assets: Path, building: dict, level: dict, state: str, cache: dict) -> dict | None:
    spec = level.get('imageLayers', {})
    if 'anim' + state not in spec:
        return None
    layers, sources = [], []
    for name in ('shadow', 'top', 'anim', 'anim2', 'anim3'):
        item = spec.get(name + state)
        if not item:
            continue
        relative = building['imageBaseURL'] + item[0]
        path = assets / relative
        if not path.is_file():
            raise ValueError(f'Missing original animation layer {relative}')
        with Image.open(path) as source_image:
            image = source_image.convert('RGBA')
        rect = item[1]
        x, y = round(rect[0]), round(rect[1])
        frame_width, frame_height = image.size
        frames = 1
        if name.startswith('anim'):
            frame_width, frame_height = round(rect[2]), round(rect[3])
            # Tower21 declares30 but its32-direction image/sourceRotate contain32.
            # Preserve actual available cells and declarations separately.
            frames = image.width // frame_width
            if frames < 1:
                raise ValueError(f'Original animation rectangle does not fit {relative}')
        elif name == 'shadow':
            alpha = ImageOps.invert(image.convert('L'))
            image = Image.new('RGBA', image.size, (0, 0, 0, 0))
            image.putalpha(alpha)
        layers.append((name, image, x, y, frame_width, frame_height, frames))
        sources.append({'layer': name, 'texture': relative, 'offset': [x, y],
                        'frameWidth': frame_width, 'frameHeight': frame_height,
                        'textureWidth': image.width, 'textureHeight': image.height,
                        'availableFrames': frames, 'declaredFrames': item[2] if len(item) > 2 else 1,
                        'sourceImageWidth': image.width, 'sourceImageHeight': image.height,
                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    signature = json.dumps(sources, sort_keys=True)
    if signature in cache:
        return dict(cache[signature])
    left = min(item[2] for item in layers)
    top = min(item[3] for item in layers)
    right = max(item[2] + item[4] for item in layers)
    bottom = max(item[3] + item[5] for item in layers)
    width, height = right - left, bottom - top
    frames = next(item[6] for item in layers if item[0] == 'anim')
    columns = min(8, frames)
    rows = math.ceil(frames / columns)
    atlas = Image.new('RGBA', (width * columns, height * rows), (0, 0, 0, 0))
    for frame in range(frames):
        target = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        for name, image, x, y, fw, fh, available in layers:
            tile = image
            if name.startswith('anim'):
                tile = image.crop(((frame % available) * fw, 0, (frame % available + 1) * fw, fh))
            target.alpha_composite(tile, (x - left, y - top))
        atlas.alpha_composite(target, ((frame % columns) * width, (frame // columns) * height))
    relative = f'derived/buildings/animations/{building["id"]}_L{level["level"]}_{state or "normal"}.png'
    dest = assets / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(dest)
    result = {'texture': relative, 'frameWidth': width, 'frameHeight': height,
              'textureWidth': atlas.width, 'textureHeight': atlas.height,
              'pixelWidth': width, 'pixelHeight': height, 'pivotX': -left, 'pivotY': -top,
              'columns': columns, 'rows': rows, 'frames': frames, 'layers': sources,
              'layout': 'linear_frame_index_in_row_major_atlas',
              'method': 'Original layer offsets; full source animation frames; multiply shadows encoded as alpha'}
    cache[signature] = result
    return dict(result)



def source_activity_frames(frame_count: int, divisors: tuple[int, ...], available_frames: int) -> list[int]:
    """One frame selector per original display tick, including held poses.

    TickFast uses if(fast divisibility) ... else if(slow divisibility), not an
    averaged FPS. The union schedule is sampled exactly and repeats only when
    both the display-tick phase and source frame counter return to their start.
    Original truncated strips retain a held last complete frame instead of an
    out-of-bounds sample; that missing source frame is separately reported.
    """
    if frame_count < 1 or available_frames < 1 or not divisors or min(divisors) < 1:
        raise ValueError('Invalid source building animation cadence')
    period = math.lcm(*divisors)
    advances = sum(any(tick % divisor == 0 for divisor in divisors) for tick in range(period))
    ticks = period * frame_count // math.gcd(advances, frame_count)
    result, next_frame, displayed = [], 0, 0
    for tick in range(ticks):
        if any(tick % divisor == 0 for divisor in divisors):
            displayed = min(next_frame, available_frames - 1)
            next_frame = (next_frame + 1) % frame_count
        result.append(displayed)
    return result


def prepare_building_activity(source: Path, assets: Path, data: Path, buildings: list, timing: dict) -> dict:
    """Original raster-layer timelines for implemented economy buildings.

    A source sheet is not automatically a loop: Silo selects a fullness pose,
    Hatchery resets when stopped, and other producers hold their last pose.
    Housing/TownHall/Flinger/MapRoom have no animated source layers.
    """
    activity = {1: 'collector', 2: 'collector', 3: 'collector', 4: 'collector',
                8: 'unlocking', 13: 'hatching', 26: 'researching'}
    cache, entries = {}, []
    for building in buildings:
        bid = building['id']
        if bid not in (*activity, 6):
            continue
        for level in building['levels']:
            animation = _building_atlas(assets, building, level, '', cache)
            if not animation:
                raise ValueError(f'Expected original animation layer for building {bid} level {level["level"]}')
            available = animation['frames']
            declared = int(level['imageLayers']['anim'][2])
            animation.update(source=f'client/scripts/BUILDING{bid}.as:TickFast/AnimFrame',
                frameSource='client/scripts/BFOUNDATION.as:AnimFrame; YARD_PROPS.as:imageData',
                directions=1, sourceFacingOffsetDegrees=0, idleFrames=[0],
                loop=True, sourceDisplayHz=timing['displayHz'], sourceDeclaredFrames=declared,
                unavailableSourceFrames=list(range(available, declared)))
            if bid == 6:
                animation.update(mode='resource_fill', fps=0, loop=False,
                    fillFrameScale=26, refreshDisplayTicks=3,
                    selection='floor(26 * sum(current resource balances) / sum(resource capacities)); clamp to last available source frame',
                    sourceSelection='client/scripts/BUILDING6.as:Update/AnimFrame',
                    sourceLastFrameLimitation='Source requests frame26 at completely full storage but the supplied strip has frames0..25; retain the last complete source frame instead of sampling outside the atlas')
            else:
                source_frames = 30 if bid == 13 else declared
                fast, slow = ((2, 7), (7,)) if bid == 13 else ((3, 10), (10,))
                animation.update(mode='activity', activityGate=activity[bid], fps=timing['displayHz'],
                    workingFrames=source_activity_frames(source_frames, fast, available),
                    workingSlowFrames=source_activity_frames(source_frames, slow, available),
                    workingDisplayTickDivisors=list(fast), slowDisplayTickDivisors=list(slow),
                    sourceFrameCycle=[min(frame, available - 1) for frame in range(source_frames)],
                    fastTickDivisor=fast[0], slowTickDivisor=slow[0],
                    sourcePlaybackFrames=source_frames,
                    pausedFrameBehavior='reset' if bid == 13 else 'hold',
                    # BFOUNDATION.ImageLoaded seeds the declared-frame range,
                    # independently of TickFast's display phase. Main-yard
                    # Hatcheries inherit this; a stopped Hatchery resets to0.
                    sourceRandomStart=True,
                    sourceRandomStartFrames=max(1, declared - 2),
                    sourceDisplayPhaseCount=5 if bid in (1, 2, 3, 4, 8) else 1,
                    unselectedSourceFrames=list(range(min(source_frames, available), available)),
                    sourceCondition={'collector': 'built, not upgrading/fortifying, producing and canFunction',
                        'hatching': 'built, not upgrading, active creature in production stage1 and canFunction',
                        'unlocking': 'built, not upgrading and a monster is unlocking',
                        'researching': 'built, not upgrading and this Academy is researching'}[activity[bid]],
                    slowCondition='attacking/defending, or original CREEPS._creepCount is nonzero',
                    playbackNote='workingFrames and workingSlowFrames contain one frame selector per40Hz display tick, including repeated selectors for source-held poses; do not collapse to unique frames')
            level['animation'] = animation
            entries.append({'building': bid, 'level': level['level'], 'texture': animation['texture'],
                'mode': animation['mode'], 'availableFrames': available, 'declaredFrames': declared,
                'unavailableSourceFrames': animation['unavailableSourceFrames']})
            if bid != 6:
                entries[-1].update(sourceRandomStartFrames=animation['sourceRandomStartFrames'],
                    sourceDisplayPhaseCount=animation['sourceDisplayPhaseCount'])
        building['activityPresentation'] = {'source': f'client/scripts/BUILDING{bid}.as',
            'mode': 'resource_fill' if bid == 6 else 'activity',
            'activityGate': activity.get(bid, 'resource_fill'),
            'damagedPolicy': 'Original damaged states have no animdamaged layer; display damaged static art, never overlay intact animation'}
    original = data / 'Original' / 'BuildingAnimation'
    original.mkdir(parents=True, exist_ok=True)
    for name in ('BUILDING1.as', 'BUILDING2.as', 'BUILDING3.as', 'BUILDING4.as', 'BUILDING6.as',
                 'BUILDING8.as', 'BUILDING13.as', 'BUILDING15.as', 'BUILDING26.as', 'BFOUNDATION.as'):
        shutil.copy2(source / 'client/scripts' / name, original / name)
    report = {'schema': 1, 'displayHz': timing['displayHz'], 'entries': entries,
        'uniqueAtlases': len(cache), 'buildingTypes': sorted((*activity, 6)),
        'staticOriginalCoreBuildings': [5, 11, 14, 15, 17, 18, 20],
        'notes': ['All animation cells use original PNG pixels and original layer offsets.',
            'Housing movement comes from resident monsters; Housing itself has no source animation layer.',
            'Production and research loops are gated by authoritative building activity.',
            'Initial source pose selects int(random * (declaredFrames - 2)); only collectors and Locker randomize their display phase0..4.',
            'Silo fullness is a state selector, not a looping animation.',
            'Hatchery source wraps at30 even though its atlas contains31 cells.',
            'Locker level4 declares21 cells but supplied art has20; missing frame20 holds frame19.',
            'Overdrive-specific frame skipping is not active until that gameplay system is implemented.']}
    (data / 'building-animation-provenance.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def prepare_combat(source: Path, assets: Path, data: Path, buildings: list, monsters: list, sprites: dict) -> dict:
    timing = source_timing(source)
    for sid, sprite in sprites.items():
        with Image.open(assets / sprite['texture']) as image:
            sprite.update(textureWidth=image.width, textureHeight=image.height)
        sprite['animations'] = sprite_animations(sid, sprite, timing)
        walk = sprite['animations']['walk']
        sprite['fps'] = walk['fps']
        sprite['fpsStatus'] = 'Source simulation tick rate divided by the exact SPRITES.GetSprite frame divisor; zero means directional pose'
        sprite['idleRow'] = (sprite['animations']['idle']['rows'] or [0])[0]
        sprite['movingStartRow'] = (walk['rows'] or [0])[0]
        sprite['movingFrames'] = max(1, len(walk['rows']))
        sprite['sourceFacingOffsetDegrees'] = -45 if sid.startswith('G') else 0
        sprite['sourceFacingOffsetSource'] = MONSTER_SOURCE + ':render adds90 back before assigning m_rotation; ChampionBase.getNextSprite subtracts45'
        used = {row for clip in sprite['animations'].values() for row in clip['rows']}
        sprite['unselectedSourceRows'] = sorted(set(range(sprite['rows'])) - used)
        sprite['sourceSimulationHz'] = timing['simulationHz']
    for monster in monsters:
        sprite = sprites.get(monster['id'], {})
        monster['sprite'] = sprite
        if sprite:
            monster.update(textureWidth=sprite['textureWidth'], textureHeight=sprite['textureHeight'])
        monster['fps'] = sprite.get('fps', 0)
        monster['attackIntervalSeconds'] = (monster.get('attackDelayFrames') or 60) / timing['simulationHz']
        monster['attackDelayTicks'] = monster.get('attackDelayFrames') or 60
        monster['sourceSimulationHz'] = timing['simulationHz']
        monster['sourceMovePixelsPerSecond'] = monster['speed'] * timing['simulationHz'] * .25
        monster['movementSpace'] = 'isometric_screen'
        monster['movementSpeedSource'] = CREEP_SOURCE + ':constructor speed/2; move speed*.5; source pixels per simulation tick'
        monster['attackIntervalSource'] = CREEP_SOURCE + ':constructor default60; attackCooldown decremented each simulationtick'
        for level in monster.get('levels', []):
            ticks = level.get('attackDelayFrames') or monster.get('attackDelayFrames') or 60
            level['attackIntervalSeconds'] = ticks / timing['simulationHz']
            level['attackDelayTicks'] = ticks
            level['sourceSimulationHz'] = timing['simulationHz']
            level['sourceMovePixelsPerSecond'] = level['speed'] * timing['simulationHz'] * .25
        monster['presentation'] = {'attackSource': CREEP_SOURCE + ':tickBAttack',
            'attackPose': 'SPRITES.GetSprite receives spriteAction walking for ordinary creeps; no separate melee attack sequence is present',
            'deathSource': MONSTER_SOURCE + ':die/dieFinish/deathSplat',
            'deathEffect': 'splat', 'deathFallSeconds': .4 if monster.get('flying') else 0}
        if monster.get('flying'):
            monster['presentation']['hover'] = {'source': CREEP_SOURCE + ':tickState',
                'amplitudePixels': 10, 'angularSpeedRadiansPerSecond': timing['simulationHz'] / 50,
                'sourceExpression': '_altitude=max-sin(frame/50)*5; graphicY=-altitude-36+sin(frame/50)*5'}
        projectile = {'C14': 'fireball', 'C15': 'healball', 'C16': 'healball', 'IC7': 'fireball'}.get(monster['id'])
        if projectile:
            monster['projectileKey'] = projectile
            monster['projectileSource'] = 'client/scripts/com/monsters/monsters/creeps/rebalance/' + {
                'C14': 'Teratornv2', 'C15': 'Zafreetiv2', 'C16': 'Vorgv2', 'IC7': 'Sabnoxv2'}[monster['id']] + '.as:rangedAttack'
    cache = {}
    for building in buildings:
        footprint = normalized_footprint(building)
        if footprint:
            building['combatFootprint'] = footprint
        for level in building['levels']:
            if level.get('attackRateTicks', 0) > 0:
                level['attackIntervalSeconds'] = level['attackRateTicks'] * 2 / timing['simulationHz']
                level['attackIntervalSource'] = 'client/scripts/BTOWER.as:TickAttack _fireTick+=_rate*2; GLOBAL.as simulation clock'
                level['sourceSimulationHz'] = timing['simulationHz']
                if building['id'] == 25:
                    level['attackIntervalSeconds'] = 4 / timing['displayHz']
                    level['attackIntervalSource'] = 'client/scripts/BUILDING25.as:TickFast active stage fires each4displayframes'
                    level['burstShots'] = level['attackRateTicks']
                    level['sourceDisplayHz'] = timing['displayHz']
        if building['id'] not in (20, 21, 23, 25):
            continue
        building['combatPresentation'] = {
            'source': f'client/scripts/BUILDING{building["id"]}.as:Fire',
            'projectile': {20: 'cannonball', 21: 'bullet', 23: 'laser', 25: 'lightning'}[building['id']],
            'sound': {20: 'splash1', 21: 'snipe1', 23: 'laser', 25: 'lightningfire'}[building['id']],
            'muzzleOffsetPixels': {20: [0, -4], 21: [0, -30], 23: [0, 35], 25: [0, -50]}[building['id']]}
        for level in building['levels']:
            animation = _building_atlas(assets, building, level, '', cache)
            if not animation:
                building['combatPresentation']['animationAvailability'] = 'source_static_building_with_projectile'
                continue
            animation.update(fps=timing['displayHz'], idleFrames=[0], attackFrames=[], directions=1,
                             source=f'client/scripts/BUILDING{building["id"]}.as', sourceFacingOffsetDegrees=0)
            if building['id'] in (21, 23):
                source_directions = 32 if building['id'] == 21 else 54
                animation.update(mode='directional', directions=min(source_directions, animation['frames']),
                                 sourceDirections=source_directions, sourceDegreesPerDirection=11.25 if building['id'] == 21 else 6.66,
                                 facingSpace='cartesian' if building['id'] == 21 else 'isometric_screen',
                                 attackFrames=list(range(min(source_directions, animation['frames']))), fps=0)
            else:
                animation.update(mode='firing', startFrames=list(range(1, 32)), attackFrames=list(range(32, 41)),
                                 endFrames=list(range(41, 55)), endFps=timing['displayHz'] / 2,
                                 sourceStages='charge increments once/ENTER_FRAME; active32..40; cooldown increments on even displayframes')
            damaged = _building_atlas(assets, building, level, 'damaged', cache)
            if damaged:
                for key in ('mode', 'fps', 'idleFrames', 'attackFrames', 'directions', 'source', 'sourceFacingOffsetDegrees',
                            'sourceDirections', 'sourceDegreesPerDirection', 'facingSpace', 'startFrames', 'endFrames', 'endFps', 'sourceStages'):
                    if key in animation:
                        damaged[key] = animation[key]
                animation['damaged'] = damaged
            level['animation'] = animation
    activity_report = prepare_building_activity(source, assets, data, buildings, timing)
    original = data / 'Original' / 'Combat'
    source_files = ['GLOBAL.as', 'BFOUNDATION.as', 'BTOWER.as', 'BUILDING20.as', 'BUILDING21.as', 'BUILDING23.as', 'BUILDING25.as',
                    'PROJECTILES.as', 'PROJECTILE.as', 'EFFECTS.as', 'com/monsters/monsters/MonsterBase.as',
                    'com/monsters/monsters/creeps/CreepBase.as', 'com/monsters/monsters/creeps/Rezghul.as',
                    'com/monsters/monsters/champions/ChampionBase.as', 'com/monsters/display/CreepSkinManager.as',
                    'com/monsters/projectiles/ProjectileUtils.as', 'com/monsters/effects/LASER.as', 'com/monsters/effects/smoke/Smoke.as']
    for name in source_files:
        destination = original / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'client/scripts' / name, destination)
    shutil.copy2(source / 'asconfig.json', original / 'asconfig.json')
    effects = prepare_effects(source, assets, timing, sprites)
    sounds = {p.stem.removeprefix('sound_'): p.relative_to(assets).as_posix()
              for p in sorted((assets / 'sounds').rglob('*.wav')) if p.parent.name != 'music'}
    report = {'schema': 1, 'timing': timing, 'effects': effects, 'sounds': sounds,
              'monsterFrameSource': SPRITE_SOURCE,
              'directionOnlyMonsters': [m['id'] for m in monsters if len(m['sprite'].get('animations', {}).get('walk', {}).get('rows', [])) <= 1],
              'missingAttackSequences': [m['id'] for m in monsters if len(m['sprite'].get('animations', {}).get('attack', {}).get('rows', [])) <= 1],
              'notes': ['Original facing columns are not walk frames.',
                        'C17/C18 atlas rows without a source GetSprite selector are preserved but not assigned invented states.',
                        'Ordinary ground-monster source death is a splat effect, not an absent death atlas.',
                        'Cannon Tower has no source animated layer; its shot is an independently animated projectile.'],
              'buildingAnimationAtlases': len(cache),
              'activityBuildingAnimationAtlases': activity_report['uniqueAtlases']}
    (data / 'combat-art-provenance.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report
