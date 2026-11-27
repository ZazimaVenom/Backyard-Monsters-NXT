#!/usr/bin/env python3
"""Offline artifact integrity checks. This does NOT compile or run Unreal Engine.
Run: python Tools/validate_source.py [--json report.json] [--skip-hashes]
No additional Python packages are required.
"""
from __future__ import annotations
import argparse
import configparser
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys


def validate(project: Path, skip_hashes: bool = False) -> dict:
    errors: list[str] = []
    warnings: list[str] = []
    checks = 0
    def check(ok: bool, message: str) -> bool:
        nonlocal checks
        checks += 1
        if not ok: errors.append(message)
        return bool(ok)
    def read_json(path: Path):
        try: return json.loads(path.read_text(encoding='utf-8-sig'))
        except (OSError, ValueError) as e:
            errors.append(f'{path.relative_to(project)}: {e}')
            return {}
    def text(path: Path):
        try: return path.read_text(encoding='utf-8-sig')
        except OSError:
            errors.append(f'Missing file: {path.relative_to(project)}')
            return ''
    plugin = project / 'Plugins/BackyardMMO'
    data = plugin / 'Content/Data'
    assets = plugin / 'SourceAssets'
    descriptor = read_json(plugin / 'BackyardMMO.uplugin')
    host = read_json(project / 'BackyardMonstersNXT.uproject')
    check(host.get('EngineAssociation') == '5.8', 'Host project must target UE 5.8.')
    check(any(p.get('Name') == 'BackyardMMO' and p.get('Enabled') for p in host.get('Plugins', [])), 'Host project must enable BackyardMMO.')
    check(descriptor.get('CanContainContent') is True, 'Plugin must allow content.')
    modules = {m.get('Name'): m for m in descriptor.get('Modules', [])}
    check(modules.get('BackyardMMO', {}).get('Type') == 'Runtime', 'Missing runtime module descriptor.')
    check(modules.get('BackyardMMOEditor', {}).get('Type') == 'Editor', 'Editor module must remain editor-only.')
    for module in modules:
        check((plugin / 'Source' / module / f'{module}.Build.cs').is_file(), f'Missing Build.cs for module {module}.')
    for path in ['Source/BackyardMonstersNXT.Target.cs', 'Source/BackyardMonstersNXTEditor.Target.cs', 'Source/BackyardMonstersNXT/BackyardMonstersNXT.Build.cs']:
        check((project / path).is_file(), f'Missing host build file: {path}')

    manifest = read_json(data / 'asset-manifest.json')
    catalog = read_json(data / 'catalog.json')
    check(manifest.get('schema') == 1, 'Unsupported/missing asset manifest schema.')
    check(catalog.get('schema') == 1, 'Unsupported/missing gameplay catalog schema.')
    by_source = {}
    by_object = {}
    importable_count = 0
    hashed_bytes = 0
    def safe_component(part: str) -> str:
        part = re.sub(r'[^A-Za-z0-9_]', '_', part)
        return ('T_' + part) if part[:1].isdigit() else (part or 'Asset')
    for row in manifest.get('assets', []):
        rel = row.get('source', '')
        pp = PurePosixPath(rel)
        if not check(bool(rel) and not pp.is_absolute() and '..' not in pp.parts and '\\' not in rel, f'Unsafe asset relative path: {rel!r}'):
            continue
        check(rel not in by_source, f'Duplicate asset source: {rel}')
        by_source[rel] = row
        source = assets.joinpath(*pp.parts)
        if not check(source.is_file(), f'Missing source asset: {rel}'): continue
        check(source.stat().st_size == row.get('bytes'), f'Asset byte count changed: {rel}')
        if not skip_hashes:
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            check(digest == row.get('sha256'), f'Asset SHA-256 mismatch: {rel}')
            hashed_bytes += source.stat().st_size
        if row.get('importable'):
            importable_count += 1
            check(pp.suffix.lower() in {'.png', '.jpg', '.jpeg', '.wav'}, f'Unsupported importable asset: {rel}')
            components = [safe_component(p) for p in pp.parts[:-1]] + [safe_component(pp.stem)]
            expected = '/BackyardMMO/Art/' + '/'.join(components) + '.' + components[-1]
            check(row.get('objectPath') == expected, f'Unstable object path mapping: {rel}')
            obj = str(row.get('objectPath', '')).casefold()
            check(obj not in by_object, f'Case-insensitive Unreal object collision: {rel} and {by_object.get(obj)}')
            by_object[obj] = rel
    check(importable_count > 1000, 'Asset payload unexpectedly small; expected original imported art inventory.')

    buildings = catalog.get('buildings', [])
    monsters = catalog.get('monsters', [])
    check(isinstance(buildings, list) and len(buildings) == 140, 'Overworld source catalog should retain all 140 records, including disabled records.')
    check(len({b.get('id') for b in buildings}) == len(buildings), 'Duplicate building IDs.')
    check(len({m.get('id') for m in monsters}) == len(monsters), 'Duplicate monster IDs.')
    def reference(rel: str, context: str, optional: bool = False):
        if not rel:
            if not optional: errors.append(f'{context}: empty required texture path')
            return
        if rel not in by_source:
            if optional: warnings.append(f'{context}: unresolved optional source asset {rel}')
            else: check(False, f'{context}: source asset absent from manifest: {rel}')
        else: check(by_source[rel].get('importable') is True, f'{context}: asset is not importable: {rel}')
    atlas_image_sizes = {}
    atlas_descriptors_checked = 0
    def authored_atlas(atlas: dict, context: str):
        """Verify authored UV denominators against PNG headers, not inferred cells.

        This remains standard-library-only. Every supplied runtime atlas is a
        PNG; the 24-byte IHDR read is independent of catalog/manifest dimensions.
        """
        nonlocal atlas_descriptors_checked
        atlas_descriptors_checked += 1
        relative = atlas.get('texture', '')
        reference(relative, context)
        if relative not in by_source: return
        if relative not in atlas_image_sizes:
            try:
                with (assets / relative).open('rb') as image:
                    header = image.read(24)
            except OSError:
                check(False, f'{context}: cannot read atlas image header: {relative}')
                return
            if not check(len(header) == 24 and header[:8] == b'\x89PNG\r\n\x1a\n' and header[12:16] == b'IHDR',
                         f'{context}: runtime atlas must have a readable original PNG header: {relative}'):
                return
            atlas_image_sizes[relative] = (int.from_bytes(header[16:20], 'big'), int.from_bytes(header[20:24], 'big'))
        actual = atlas_image_sizes[relative]
        manifest_image = by_source[relative]
        check((manifest_image.get('width'), manifest_image.get('height')) == actual,
              f'{context}: manifest atlas dimensions differ from the actual PNG header.')
        valid = True
        for field, expected in zip(('textureWidth', 'textureHeight'), actual):
            value = atlas.get(field)
            valid = check(isinstance(value, int) and not isinstance(value, bool) and value > 0 and value == expected,
                          f'{context}: {field} must equal the complete source image dimension {expected}, including padding.') and valid
        for field in ('frameWidth', 'frameHeight'):
            value = atlas.get(field)
            valid = check(isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= 1,
                          f'{context}: invalid authored atlas {field}.') and valid
        for field in ('columns', 'rows', 'frames'):
            value = atlas.get(field)
            valid = check(isinstance(value, int) and not isinstance(value, bool) and value >= 1,
                          f'{context}: invalid authored atlas {field}.') and valid
        if not valid: return
        check(atlas['frameWidth'] * atlas['columns'] <= actual[0] + 1e-8
              and atlas['frameHeight'] * atlas['rows'] <= actual[1] + 1e-8,
              f'{context}: valid source cells extend outside the authored atlas dimensions.')
        check(atlas['frames'] <= atlas['columns'] * atlas['rows'],
              f'{context}: declared frame count exceeds authored atlas cell capacity.')
    numeric_keys = ['twigs', 'pebbles', 'putty', 'goo', 'time', 'hp', 'production', 'capacity', 'damage', 'range']
    unavailable = []
    for b in buildings:
        bid = b.get('id')
        if not b.get('texture'): unavailable.append(bid)
        else: reference(b['texture'], f'Building {bid}')
        levels = b.get('levels', [])
        check([v.get('level') for v in levels] == list(range(1, len(levels)+1)), f'Building {bid}: non-sequential levels.')
        for level in levels:
            for key in numeric_keys:
                val = level.get(key)
                check(isinstance(val, (int, float)) and math.isfinite(val) and val >= 0, f'Building {bid} level {level.get("level")}: invalid {key}.')
            if level.get('texture'): reference(level['texture'], f'Building {bid} level {level.get("level")}')
            for state in ['composite', 'damagedComposite', 'destroyedComposite']:
                composite = level.get(state)
                if not composite: continue
                context = f'Building {bid} level {level.get("level")} {state}'
                reference(composite.get('texture', ''), context)
                original = by_source.get(composite.get('texture'), {})
                check(composite.get('pixelWidth') == original.get('width') and composite.get('pixelHeight') == original.get('height'), f'{context}: composite dimensions differ from prepared image.')
                for axis in ['pivotX', 'pivotY']:
                    value = composite.get(axis)
                    check(isinstance(value, (int, float)) and math.isfinite(value), f'{context}: invalid source pivot {axis}.')
            for req in level.get('requirements', []):
                check(isinstance(req, list) and len(req) == 3 and all(isinstance(v, (int, float)) for v in req), f'Building {bid}: malformed prerequisite tuple.')
    if unavailable: warnings.append('Source records without usable building art remain unavailable: ' + ', '.join(map(str, unavailable)))
    for m in monsters:
        mid = m.get('id')
        reference(m.get('texture', ''), f'Monster {mid}', optional=True)
        for art_key in ['menuTexture', 'portraitTexture', 'mediumTexture']:
            reference(m.get(art_key, ''), f'Monster {mid} {art_key}', optional=True)
        for key in ['hp', 'damage', 'speed', 'housing', 'goo', 'time']:
            val = m.get(key)
            check(isinstance(val, (int, float)) and math.isfinite(val) and (key == 'damage' or val >= 0), f'Monster {mid}: invalid {key}.')
        levels = m.get('levels', [])
        research = m.get('research', [])
        unlock = m.get('unlock', {})
        check(isinstance(levels, list) and 1 <= len(levels) <= 20, f'Monster {mid}: missing bounded progression levels.')
        check(isinstance(research, list) and len(research) == len(levels) - 1, f'Monster {mid}: research transitions do not cover all levels.')
        for index, level in enumerate(levels, 1):
            check(level.get('level') == index, f'Monster {mid}: non-sequential level index.')
            for key in ['hp', 'damage', 'speed', 'range', 'housing', 'goo', 'time', 'attackDelayFrames']:
                val = level.get(key)
                check(isinstance(val, (int, float)) and math.isfinite(val) and (key == 'damage' or val >= 0), f'Monster {mid} level {index}: invalid {key}.')
        for index, transition in enumerate(research, 1):
            check(transition.get('fromLevel') == index and transition.get('toLevel') == index + 1 and transition.get('requiredAcademyLevel') == index,
                  f'Monster {mid}: invalid research level/Academy transition.')
            for key in ['putty', 'time']:
                val = transition.get(key)
                check(isinstance(val, (int, float)) and math.isfinite(val) and val >= 0, f'Monster {mid}: invalid research {key}.')
        for key in ['putty', 'time', 'lockerLevel']:
            val = unlock.get(key)
            check(isinstance(val, (int, float)) and math.isfinite(val) and val >= 0, f'Monster {mid}: invalid unlock {key}.')
        check(isinstance(unlock.get('initiallyUnlocked'), bool), f'Monster {mid}: missing initial unlock state.')
        check(isinstance(m.get('sourceBlocked'), bool) and isinstance(m.get('sourceFake'), bool), f'Monster {mid}: missing source availability flags.')
        sprite = m.get('sprite', {})
        if sprite:
            reference(sprite.get('texture', ''), f'Monster {mid} sprite')
            reference(sprite.get('idleTexture', ''), f'Monster {mid} idle thumbnail')
            image = by_source.get(sprite.get('texture'), {})
            for dim, count, total in [('frameWidth', 'columns', 'width'), ('frameHeight', 'rows', 'height')]:
                frame, frames, pixels = sprite.get(dim, 0), sprite.get(count, 0), image.get(total, 0)
                check(frame > 0 and frames >= 1 and frame * frames <= pixels + 0.001, f'Monster {mid}: atlas {dim}/{count} exceeds source dimensions.')
            check(sprite.get('layout') == 'columns_are_facing_directions_rows_are_animation_states', f'Monster {mid}: unexpected directional atlas schema.')
            expected_offset = -45 if mid.startswith('G') else 0
            check(sprite.get('sourceFacingOffsetDegrees') == expected_offset, f'Monster {mid}: source render facing transform differs.')
            animations = sprite.get('animations', {})
            for state in ['idle', 'walk', 'attack', 'death']:
                clip = animations.get(state, {})
                check(isinstance(clip.get('rows'), list), f'Monster {mid}: missing {state} clip metadata.')
                for row in clip.get('rows', []):
                    check(isinstance(row, int) and 0 <= row < sprite.get('rows', 0), f'Monster {mid}: {state} selects an unavailable atlas row.')
                rate = clip.get('fps')
                check(isinstance(rate, (int, float)) and math.isfinite(rate) and rate >= 0, f'Monster {mid}: invalid {state} frame rate.')
                check(clip.get('availability') in {'source_frames', 'directional_only', 'no_source_sequence'}, f'Monster {mid}: {state} source availability is not explicit.')
    # Cover top-level fallback descriptors as well as the nested descriptors
    # normally used by the Board. Effects and composed defense sheets use the
    # same authored-size contract even when their last atlas row is partial.
    sprite_catalog = catalog.get('sprites', {})
    check(len(sprite_catalog) == 65, 'Source sprite catalog should preserve all 65 descriptors.')
    for sprite_id, sprite in sprite_catalog.items():
        authored_atlas(sprite, f'Sprite {sprite_id}')
    for monster in monsters:
        authored_atlas(monster, f'Monster {monster.get("id")} fallback atlas')
        authored_atlas(monster.get('sprite', {}), f'Monster {monster.get("id")} sprite atlas')
    for building in buildings:
        for level in building.get('levels', []):
            relationships = level.get('mapRoomRelationshipAnimations', {})
            if relationships:
                check(building.get('id') == 140 and set(relationships) == {'self', 'enemy', 'ally', 'neutral'},
                      'Map Room defender must retain all four original relationship variants.')
                for relationship, descriptor in relationships.items():
                    context = f'Building 140 level {level.get("level")} {relationship} animation'
                    authored_atlas(descriptor, context)
                    check(descriptor.get('frames') == 32 and descriptor.get('columns') == 8 and descriptor.get('rows') == 4,
                          f'{context}: original 32-frame relationship sheet changed.')
                    check(descriptor.get('sourceFrameCycle') == list(range(32)) and descriptor.get('sourceDisplayHz') == 40,
                          f'{context}: original relationship playback cycle changed.')
            animation = level.get('animation')
            if not animation: continue
            context = f'Building {building.get("id")} level {level.get("level")} animation'
            authored_atlas(animation, context)
            if animation.get('damaged'):
                authored_atlas(animation['damaged'], context + ' damaged')
            if animation.get('mode') == 'activity':
                cycle = animation.get('sourceFrameCycle', [])
                check(isinstance(cycle, list) and 1 <= len(cycle) <= 65536,
                      f'{context}: missing bounded original activity cycle.')
                check(all(isinstance(frame, int) and 0 <= frame < animation['frames'] for frame in cycle),
                      f'{context}: original activity cycle escapes authored frames.')
                check(animation.get('sourceDisplayHz') == 40, f'{context}: original display cadence changed.')
                expected_fast, expected_slow = (2, 7) if building.get('id') == 13 else (3, 10)
                check(animation.get('fastTickDivisor') == expected_fast and animation.get('slowTickDivisor') == expected_slow,
                      f'{context}: TickFast cadence no longer matches original source.')
                check(animation.get('pausedFrameBehavior') == ('reset' if building.get('id') == 13 else 'hold'),
                      f'{context}: original pause frame behavior changed.')
            if animation.get('mode') == 'resource_fill':
                check(building.get('id') == 6 and animation.get('fillFrameScale') == 26 and animation.get('fps') == 0,
                      f'{context}: Silo must select source fill cells instead of looping.')
            if animation.get('mode') == 'decoration':
                frames = animation.get('idleFrames', [])
                check(building.get('type') == 'decoration' and animation.get('fps') == 20
                      and animation.get('sourceDisplayHz') == 40 and animation.get('sourceTickDivisor') == 2,
                      f'{context}: decoration playback must follow the original half-rate display tick.')
                check(isinstance(frames, list) and bool(frames)
                      and all(isinstance(frame, int) and 0 <= frame < animation.get('frames', 0) for frame in frames),
                      f'{context}: decoration cycle escapes authored cells.')
            if animation.get('mode') == 'bunker_door':
                check(building.get('id') == 22 and animation.get('frames') == 15
                      and animation.get('columns') == 8 and animation.get('rows') == 2,
                      f'{context}: Bunker must retain its fifteen authored door cells.')
                check(animation.get('sourceDisplayHz') == 40 and animation.get('sourcePhaseMaximum') == 15
                      and animation.get('closedFrame') == 0 and animation.get('openFrame') == 14
                      and animation.get('loop') is False,
                      f'{context}: source Bunker endpoint must clamp to cell 14 and never loop.')
                check((animation.get('frameWidth'), animation.get('frameHeight'), animation.get('pivotX'), animation.get('pivotY'))
                      == (138, 100, 66, 15) and not animation.get('damaged'),
                      f'{context}: Bunker original layer bounds/pivot or damaged fallback changed.')
    for effect_id, effect in catalog.get('effects', {}).items():
        authored_atlas(effect, f'Effect {effect_id}')
    worker_sprite = sprite_catalog.get('worker', {})
    authored_atlas(worker_sprite, 'Original worker direction sheet')
    check((worker_sprite.get('textureWidth'), worker_sprite.get('textureHeight'),
           worker_sprite.get('frameWidth'), worker_sprite.get('frameHeight')) == (810, 54, 27, 27),
          'Worker sheet must keep original directional cells and both normal/boosted rows.')
    mushroom = next((b for b in buildings if b.get('id') == 7), {})
    variants = mushroom.get('mushroomPresentation', {}).get('variants', [])
    check(len(variants) == 5 and [v.get('frame') for v in variants] == [1, 2, 3, 4, 5],
          'Mushrooms require all five original visual variants.')
    for variant in variants:
        for layer in ('top', 'shadow'):
            reference(variant.get(layer, {}).get('texture', ''), f'Mushroom {variant.get("frame")} {layer}')
    for sprite_id, width, height, covered_width, covered_height in [
            ('C13', 1280, 27, 1280, 26), ('C18', 960, 116, 960, 100),
            ('IC2', 1933, 46, 1932, 46), ('IC6', 1718, 39, 1710, 39)]:
        sprite = sprite_catalog.get(sprite_id, {})
        check((sprite.get('textureWidth'), sprite.get('textureHeight')) == (width, height),
              f'{sprite_id}: exact padded source image dimensions changed.')
        check(math.isclose(sprite.get('frameWidth', 0) * sprite.get('columns', 0), covered_width, abs_tol=1e-8)
              and math.isclose(sprite.get('frameHeight', 0) * sprite.get('rows', 0), covered_height, abs_tol=1e-8),
              f'{sprite_id}: source padding must not become extra or stretched animation cells.')
    # Independent source spot-checks, not a self-consistency test of generated data.
    twig = next((b for b in buildings if b.get('id') == 1), {})
    first = (twig.get('levels') or [{}])[0]
    check(first.get('pebbles') == 750 and first.get('time') == 15 and first.get('hp') == 500, 'Twigsnapper source values changed (750 Pebbles / 15 s / 500 HP).')
    pokey = next((m for m in monsters if m.get('id') == 'C1'), {})
    check(pokey.get('hp') == 200 and pokey.get('damage') == 60 and pokey.get('goo') == 250, 'Pokey source base values changed (200 HP / 60 damage / 250 Goo).')
    terrain = catalog.get('terrain', {})
    for key, rel in terrain.items():
        if isinstance(rel, str): reference(rel, f'Terrain {key}')
        elif isinstance(rel, list):
            for i, path in enumerate(rel): reference(path, f'Terrain {key}[{i}]')
    for rel in catalog.get('music', []): reference(rel, 'Music')
    for name in ['YARD_PROPS.as', 'INFERNOYARDPROPS.as', 'OUTPOST_YARD_PROPS.as', 'CREATURELOCKER.as', 'SOURCE_LICENSE', 'SOURCE_README.md']:
        check((data / 'Original' / name).is_file(), f'Original provenance missing: {name}')

    source_root = plugin / 'Source'
    bunker_provenance = read_json(data / 'Original/Bunker/provenance.json')
    check(bunker_provenance.get('schema') == 1
          and bunker_provenance.get('branch') == 'ordinary_non_mr3_overworld_main_yard',
          'Bunker provenance must identify the ordinary count-based source branch.')
    bunker_sources = bunker_provenance.get('sources', {})
    check(isinstance(bunker_sources, dict) and len(bunker_sources) == 11,
          'Bunker provenance must retain all eleven source references.')
    if isinstance(bunker_sources, dict):
        for original_path, metadata in bunker_sources.items():
            if not check(isinstance(metadata, dict), f'Invalid Bunker provenance entry: {original_path}'): continue
            filename = metadata.get('file', '')
            if not check(isinstance(filename, str) and bool(filename) and PurePosixPath(filename).name == filename
                         and '\\' not in filename, f'Unsafe Bunker reference filename: {filename!r}'): continue
            source = data / 'Original/Bunker' / filename
            if not check(source.is_file(), f'Missing original Bunker reference: {original_path}'): continue
            raw = source.read_bytes()
            check(len(raw) == metadata.get('bytes') and hashlib.sha256(raw).hexdigest() == metadata.get('sha256'),
                  f'Original Bunker reference changed: {original_path}')
    bunker_rules = bunker_provenance.get('rules', {})
    check(bunker_rules.get('capacity') == [380, 450, 540, 660, 800]
          and bunker_rules.get('rawMapRoom3Capacity') == [190, 225, 270, 330, 400],
          'Ordinary Bunker runtime capacities must remain distinct from the raw MR3 catalog.')
    bunker = next((building for building in buildings if building.get('id') == 22), {})
    check([level.get('capacity') for level in bunker.get('levels', [])] == [190, 225, 270, 330, 400],
          'Preserve raw source Bunker capacity rows; the ordinary override belongs to its runtime rules.')
    check(len(bunker.get('levels', [])) == 5
          and all(level.get('animation', {}).get('mode') == 'bunker_door' for level in bunker.get('levels', [])),
          'All five source Bunker levels must use the original door animation.')
    experience = read_json(data / 'experience.json')
    thresholds = experience.get('thresholds', [])
    valid_thresholds = (isinstance(thresholds, list) and len(thresholds) == 100
                        and all(isinstance(value, int) and not isinstance(value, bool)
                                and 0 <= value <= 9007199254740991 for value in thresholds))
    check(valid_thresholds, 'Experience must preserve 100 exact, non-negative client thresholds.')
    if valid_thresholds:
        check(thresholds[0] == 0 and thresholds[-1] == 755984118210000
              and all(a < b for a, b in zip(thresholds, thresholds[1:])),
              'Experience threshold boundaries/order differ from the active Flash client.')
        compiled_thresholds = [int(value) for value in re.findall(r'\b(\d+)LL\b',
            text(source_root / 'BackyardMMO/Private/BYMExperienceThresholds.inl'))]
        check(compiled_thresholds == thresholds, 'Compiled experience thresholds differ from the preserved data.')
        check(experience.get('suppliedServerThresholds') == thresholds[:56], 'Source server threshold discrepancy changed.')
    check(experience.get('schema') == 1 and experience.get('maximumDisplayedLevel') == 99
          and experience.get('finalThresholdIsUpperBoundary') is True,
          'Experience level range must follow the active client, including its final upper boundary.')
    experience_sources = experience.get('sources', {})
    check(isinstance(experience_sources, dict) and len(experience_sources) == 8,
          'Experience provenance must preserve all eight original source references.')
    if isinstance(experience_sources, dict):
        for relative, metadata in experience_sources.items():
            original = data / 'Original/Experience' / PurePosixPath(relative).name
            if not check(original.is_file(), f'Missing experience source: {relative}'): continue
            raw = original.read_bytes()
            check(isinstance(metadata, dict) and len(raw) == metadata.get('bytes')
                  and hashlib.sha256(raw).hexdigest() == metadata.get('sha256'),
                  f'Experience original source changed: {relative}')
    code_files = sorted(source_root.rglob('*.h')) + sorted(source_root.rglob('*.cpp')) + sorted(source_root.rglob('*.inl')) + sorted(source_root.rglob('*.cs'))
    for file in code_files:
        content = text(file)
        includes = re.findall(r'^\s*#\s*include\s+[<"]([^">]+)[">]', content, re.M)
        # Lexical delimiter check catches truncated edits/Slate nesting errors; it is not a C++ parser.
        if file.suffix in {'.h', '.cpp', '.inl'}:
            lexical = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', content)
            stack = []
            pairs = {')':'(', ']':'[', '}':'{'}
            balanced = True
            for character in lexical:
                if character in '([{': stack.append(character)
                elif character in ')]}':
                    if not stack or stack.pop() != pairs[character]: balanced = False; break
            check(balanced and not stack, f'{file.name}: unbalanced C++ lexical delimiters (manual review required).')
        generated = [inc for inc in includes if inc.endswith('.generated.h')]
        if file.suffix == '.h' and re.search(r'\bU(?:CLASS|STRUCT|ENUM)\s*\(', content):
            check(generated == [file.stem + '.generated.h'], f'{file.name}: missing or wrong generated header.')
            check(bool(includes) and includes[-1] == file.stem + '.generated.h', f'{file.name}: generated header must be last include.')
        if file.suffix == '.h':
            for rpc in re.finditer(r'UFUNCTION\s*\([^)]*\b(?:Server|Client|NetMulticast)\b[^)]*\)\s*void\s+(\w+)\s*\(', content):
                cpp = file.parent.parent / 'Private' / (file.stem + '.cpp')
                check(cpp.is_file() and rpc[1] + '_Implementation(' in cpp.read_text(encoding='utf-8'), f'{file.name}: missing RPC implementation for {rpc[1]}.')
        check(not re.search(r'/workspace/|/home/agent/|[A-Za-z]:\\(?:Users|workspace|Unreal)', content), f'{file.name}: environment-specific absolute filesystem path.')
        check(not re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', content), f'{file.name}: embedded private key material.')
        check(not re.search(r'UE_LOG\s*\([^;]*\*(?:PendingPassword|Password|Plaintext)', content, re.S), f'{file.name}: potential credential logging.')
    expected_files = ['BYMBoard', 'BYMCameraPawn', 'BYMPassword', 'BYMGameInstance', 'BYMGameMode', 'BYMHUD', 'BYMMenuGameMode', 'BYMPlayerController', 'BYMServerSubsystem', 'BYMSettings', 'BYMSimulation', 'BYMYardResidents', 'BYMSpriteAtlas', 'BYMEconomy', 'BYMCombatRules', 'BYMBuildingAnimation', 'BYMBuildingLifecycle', 'BYMExperience', 'BYMBunkerRules']
    for stem in expected_files:
        check((source_root / 'BackyardMMO/Public' / (stem + '.h')).is_file(), f'Missing runtime header: {stem}.h')
        check((source_root / 'BackyardMMO/Private' / (stem + '.cpp')).is_file(), f'Missing runtime implementation: {stem}.cpp')
    # A source file can exist while an implementation is still incomplete.
    runtime_methods = {
        'FBYMSimulation': ('BYMSimulation.cpp', ['Initialize','Serialize','AddPlayer','RemovePlayer','HasYard','HasActiveBattles','Tick','Execute','Snapshot','Ladder']),
        'UBYMServerSubsystem': ('BYMServerSubsystem.cpp', ['StartAuthority','IsAuthorityRunning','StopAuthority','SaveNow','AuthenticateAsync','Disconnect','Execute','GetSnapshot','GetLadder']),
        'ABYMBoard': ('BYMBoard.cpp', ['ApplySnapshot','AssetObjectPath','GetBuildingAtCell']),
    }
    for cls, (filename, methods) in runtime_methods.items():
        implementation = text(source_root / 'BackyardMMO/Private' / filename)
        for method in methods:
            check(bool(re.search(r'\b' + cls + r'::' + method + r'\s*\(', implementation)), f'{filename}: required API implementation missing: {cls}::{method}.')
    atlas_helper_header = text(source_root / 'BackyardMMO/Public/BYMSpriteAtlas.h')
    atlas_helper_source = text(source_root / 'BackyardMMO/Private/BYMSpriteAtlas.cpp')
    for method in ('MakeLayout', 'FrameOffset'):
        check(bool(re.search(r'\b' + method + r'\s*\(', atlas_helper_header))
              and bool(re.search(r'\b' + method + r'\s*\(', atlas_helper_source)),
              f'BYMSpriteAtlas: authored UV helper API missing: {method}.')
    board = text(source_root / 'BackyardMMO/Private/BYMBoard.cpp')
    check('BYMBuildingAnimation::SelectFrame' in board and 'bActivityWorking' in board,
          'Board must connect original activity animation to the host working state.')
    controller = text(source_root / 'BackyardMMO/Private/BYMPlayerController.cpp')
    check('TEXT("cancelTraining")' in controller, 'Per-Hatchery cancellation is missing from authorized controller actions.')

    hud = text(source_root / 'BackyardMMO/Private/BYMHUD.cpp')
    check('Data/asset-manifest.json' in hud and 'SourceImageSize' in hud,
          'UI image layout must use canonical source dimensions before texture resources are ready.')
    check(not re.search(r'\bGetSize[XY]\s*\(', hud),
          'UI must not cache temporary texture-resource dimensions as its artwork aspect ratio.')
    check(not re.search(r'\.AutoWrapText\s*\(\s*true\s*\)', hud) and '.WrapTextAt(' in hud,
          'UI labels must avoid desired-size auto-wrap feedback; paragraphs require explicit wrap widths.')
    ui_layout_tests = text(source_root / 'BackyardMMOEditor/Private/Tests/BYMUILayoutTests.cpp')
    for test in ('BackyardMMO.UI.LoginLayoutAcrossResizeAndDPI',
                 'BackyardMMO.UI.SharedDialogAndHUDContainment',
                 'BackyardMMO.UI.CanonicalImageAspectDuringTextureReadiness'):
        check(test in ui_layout_tests, f'Missing native UI layout regression test: {test}.')

    check('BYMSpriteAtlas::MakeLayout' in board and 'BYMSpriteAtlas::FrameOffset' in board,
          'Board must use the authored UV helper for atlas initialization and frame selection.')
    check('TEXT("textureWidth")' in board and 'TEXT("textureHeight")' in board,
          'Board must consume the complete authored texture dimensions.')
    check('ABYMBoard::TryConfigureAtlas' in board and 'PendingAtlas' in board and 'bAtlasReady' in board,
          'Board must retain a retry path for atlases whose texture resources are not ready.')
    atlas_tests = text(source_root / 'BackyardMMO/Private/Tests/BYMSpriteAtlasTests.cpp')
    presentation_tests = text(source_root / 'BackyardMMOEditor/Private/Tests/BYMSpriteAtlasPresentationTests.cpp')
    for test in ('BackyardMMO.Presentation.SpriteAtlasLayout', 'BackyardMMO.Presentation.SpriteAtlasValidation'):
        check(test in atlas_tests, f'Missing authored atlas engine automation test: {test}.')
    check('BackyardMMO.Presentation.ResidentSpriteAtlasUVs' in presentation_tests,
          'Missing resident sprite/material UV integration automation test.')
    local_headers = {f.name for f in source_root.rglob('*.h')}
    local_fragments = {f.name for f in source_root.rglob('*.inl')}
    for file in code_files:
        for include in re.findall(r'^\s*#\s*include\s+"(BYM[^"]+\.h)"', text(file), re.M):
            if not include.endswith('.generated.h'):
                check(include in local_headers, f'{file.name}: missing plugin-local include {include}.')
        for include in re.findall(r'^\s*#\s*include\s+"(BYM[^"]+\.inl)"', text(file), re.M):
            check(include in local_fragments, f'{file.name}: missing plugin-local implementation fragment {include}.')
    settings = text(source_root / 'BackyardMMO/Public/BYMSettings.h')
    for name, value in [('MaxPlayers','100'),('RivalBotCount','1000'),('AutosaveSeconds','3600'),('ShieldSeconds','3600'),('MaxVisibleYardMonsters','50')]:
        check(bool(re.search(r'\b'+name+r'\s*=\s*'+value+r'(?:\.0f)?\s*;', settings)), f'Required default differs in settings header: {name}={value}')
    check(bool(re.search(r'\bbClampRPAtZero\s*=\s*false\s*;', settings)), 'RP defaults must allow losses below the initial zero score.')
    settings_names = set(re.findall(r'\b(?:FString|int32|float|bool)\s+(\w+)\s*=', settings))
    for file in [file for file in code_files if file.suffix in {'.cpp', '.inl'}]:
        for name in re.findall(r'GetDefault<UBYMSettings>\(\)->(\w+)', text(file)):
            check(name in settings_names, f'{file.name}: references undefined settings field {name}.')
    ini = configparser.ConfigParser(strict=False)
    ini.optionxform = str
    try: ini.read(project / 'Config/DefaultGame.ini', encoding='utf-8')
    except configparser.Error as e: errors.append('DefaultGame.ini parse error: ' + str(e))
    section = '/Script/BackyardMMO.BYMSettings'
    for name, value in [('MaxPlayers',100),('RivalBotCount',1000),('AutosaveSeconds',3600),('ShieldSeconds',3600),('MaxVisibleYardMonsters',50),('CombatSimulationStepSeconds',.1),('BattleSnapshotIntervalSeconds',.2)]:
        try: actual = ini.getfloat(section, name)
        except (ValueError, configparser.Error): actual = None
        check(actual == value, f'Required packaged default differs: {name}={value}.')
    try: clamp_rp = ini.getboolean(section, 'bClampRPAtZero')
    except (ValueError, configparser.Error): clamp_rp = None
    check(clamp_rp is False, 'Packaged RP defaults must allow losses below the initial zero score.')
    build = text(source_root / 'BackyardMMO/BackyardMMO.Build.cs')
    check('RuntimeDependencies.Add' in build and 'StagedFileType.UFS' in build, 'Runtime JSON data must be staged into packaged content.')
    check('"UnrealEd"' not in build and '"AssetTools"' not in build, 'Runtime module must not depend on editor modules.')
    check(not re.search(r'OpenSSL|bcrypt|libcrypto|PlatformCrypto|SSL', build, re.I), 'Runtime module must not add an external cryptography dependency.')
    check(not (source_root / 'BackyardMMO/Public/BYMCrypto.h').exists(), 'Removed custom login transport must not be bundled.')
    account_header = text(source_root / 'BackyardMMO/Public/BYMAccountSaveGame.h')
    check('public USaveGame' in account_header and 'UPROPERTY(SaveGame)' in account_header, 'Accounts require native typed Unreal SaveGame storage.')
    server = text(source_root / 'BackyardMMO/Private/BYMServerSubsystem.cpp')
    check('SaveGameToMemory' in server and 'LoadGameFromMemory' in server, 'Account save must use native Unreal serialization.')
    check('BackyardMonstersNXT_Accounts.sav' in server, 'Native account save filename missing.')
    engine_ini = text(project / 'Config/DefaultEngine.ini')
    check('r.CustomDepth=3' in engine_ini, 'Custom depth stencil project setting missing.')
    check('GameInstanceClass=/Script/BackyardMMO.BYMGameInstance' in engine_ini, 'Native game instance not configured.')
    for file in project.rglob('*'):
        if file.is_file() and file.name in {'world.json', 'world.json.bak', 'world.previous.json', 'BackyardMonstersNXT_Accounts.sav', 'BackyardMonstersNXT_Accounts_Previous.sav', 'host-private.pem', 'host-private-key.pem', 'host-key.pem'}:
            check(False, f'Private host state accidentally bundled: {file.relative_to(project)}')
    warnings.append('Unreal Engine compilation, editor import, packaged gameplay, network security and 100-client/1,000-bot performance were NOT tested by this script.')
    return {'scope':'offline source and asset integrity only', 'unreal_compiled':False, 'runtime_tested':False,
            'passed':not errors, 'checks':checks, 'manifest_assets':len(by_source), 'importable_assets':importable_count,
            'atlas_descriptors_checked':atlas_descriptors_checked, 'atlas_images_checked':len(atlas_image_sizes),
            'experience_thresholds':len(thresholds) if isinstance(thresholds, list) else 0,
            'asset_bytes_hashed':hashed_bytes, 'building_records':len(buildings), 'monster_records':len(monsters),
            'errors':errors, 'warnings':warnings}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--json', type=Path, help='Write a JSON report to this file.')
    parser.add_argument('--skip-hashes', action='store_true', help='Faster metadata-only asset check; not a full integrity check.')
    args = parser.parse_args()
    result = validate(args.project.resolve(), args.skip_hashes)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if result['passed'] else 'FAIL') + ': offline source/asset validation only')
    print(f"{result['checks']} checks; {result['manifest_assets']} assets; {result['building_records']} building records; {result['monster_records']} monster records")
    for message in result['errors']: print('ERROR:', message)
    for message in result['warnings']: print('WARNING:', message)
    print('Unreal compilation and runtime tests: NOT RUN')
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
