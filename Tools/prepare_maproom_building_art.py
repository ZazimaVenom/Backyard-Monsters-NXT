#!/usr/bin/env python3
"""Compose the original Outpost Defender relationship lights, without invented frames."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import shutil
from pathlib import Path
from PIL import Image, ImageChops, ImageOps

RELATIONSHIPS = ('self', 'enemy', 'ally', 'neutral')
SOURCE_FILES = ('client/scripts/OutpostDefender.as', 'client/scripts/BFOUNDATION.as', 'client/scripts/GLOBAL.as',
                'client/scripts/GuardTower.as')


def relationship_atlas(assets: Path, building: dict, level: dict, relationship: str, cache: dict) -> dict:
    specification = level['imageLayers']
    layers = []
    inputs = []
    for name in ('shadow', 'top', 'anim'):
        item = specification[name]
        relative = building['imageBaseURL'] + (relationship+'.light.png' if name == 'anim' else item[0])
        path = assets/relative
        original = Image.open(path).convert('RGBA')
        x, y = (round(value) for value in item[1][:2])
        width, height = original.size
        count = 1
        if name == 'anim':
            width, height = (round(value) for value in item[1][2:4])
            count = int(item[2])
            if original.size != (width*count, height):
                raise ValueError(f'Relationship sheet does not contain exact declared cells: {relative}')
        image = original
        if name == 'shadow':
            # Flash multiply with existing PNG transparency: encode attenuation
            # as black-alpha, retaining its original per-pixel opacity.
            alpha = ImageChops.multiply(ImageOps.invert(original.convert('L')), original.getchannel('A'))
            image = Image.new('RGBA', original.size, (0,0,0,0))
            image.putalpha(alpha)
        layers.append((name, image, x, y, width, height, count))
        inputs.append({'layer': name, 'texture': relative, 'offset': [x,y], 'frameWidth': width,
                       'frameHeight': height, 'availableFrames': count, 'declaredFrames': count,
                       'textureWidth': original.width, 'textureHeight': original.height,
                       'sourceImageWidth': original.width, 'sourceImageHeight': original.height,
                       'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    signature = json.dumps(inputs, sort_keys=True)
    if signature in cache:
        return dict(cache[signature])
    left = min(layer[2] for layer in layers)
    top = min(layer[3] for layer in layers)
    right = max(layer[2]+layer[4] for layer in layers)
    bottom = max(layer[3]+layer[5] for layer in layers)
    width, height = right-left, bottom-top
    frames = layers[-1][6]
    columns, rows = min(frames, 8), math.ceil(frames/8)
    atlas = Image.new('RGBA', (width*columns, height*rows), (0,0,0,0))
    for frame in range(frames):
        canvas = Image.new('RGBA', (width,height), (0,0,0,0))
        for name, image, x, y, frame_width, frame_height, count in layers:
            cell = image.crop((frame*frame_width,0,(frame+1)*frame_width,frame_height)) if name == 'anim' else image
            canvas.alpha_composite(cell, (x-left,y-top))
        atlas.alpha_composite(canvas, ((frame%columns)*width,(frame//columns)*height))
    relative = f'derived/buildings/animations/{building["id"]}_L{level["level"]}_relationship_{relationship}.png'
    destination = assets/relative
    destination.parent.mkdir(parents=True,exist_ok=True)
    atlas.save(destination)
    result = {'texture': relative, 'frameWidth': width, 'frameHeight': height,
              'textureWidth': atlas.width, 'textureHeight': atlas.height, 'pixelWidth': width, 'pixelHeight': height,
              'pivotX': -left, 'pivotY': -top, 'columns': columns, 'rows': rows, 'frames': frames, 'layers': inputs,
              'layout': 'linear_frame_index_in_row_major_atlas',
              'method': 'Original top and relationship-light pixels at source offsets; multiply shadow includes original alpha',
              'mode': 'activity', 'activityGate': 'relationship_light', 'fps': 40,
              'directions': 1, 'idleFrames': [0], 'sourceDisplayHz': 40, 'sourceDeclaredFrames': frames,
              'sourcePlaybackFrames': frames, 'sourceFrameCycle': list(range(frames)),
              'workingFrames': list(range(frames)), 'workingSlowFrames': list(range(frames)),
              'workingDisplayTickDivisors': [1], 'slowDisplayTickDivisors': [1],
              'fastTickDivisor': 1, 'slowTickDivisor': 1, 'sourceRandomStart': False,
              'sourceDisplayPhaseCount': 1, 'pausedFrameBehavior': 'hold', 'loop': True,
              'unavailableSourceFrames': [], 'source': 'client/scripts/OutpostDefender.as:TickFast/setLightFromRelationship',
              'frameSource': 'client/scripts/BFOUNDATION.as:AnimFrame',
              'sourceCondition': 'Intact building relationship light; independent of resource production and battle state',
              'relationship': relationship}
    cache[signature] = result
    return dict(result)


def prepare_maproom_building_art(source: Path, assets: Path, data: Path, buildings: list) -> dict:
    stronghold = next(item for item in buildings if item['id'] == 138)
    stronghold['combatPresentation'] = {
        'source': 'client/scripts/GuardTower.as:GuardTowerTesla.fire',
        'projectile': 'lightning', 'sound': 'lightningfire', 'muzzleOffsetPixels': [0, 0],
        'emitterOrigin': 'Authoritative sourceX/sourceY for each of the four independent source Tesla emitters',
        'animationAvailability': 'Source imageData supplies only intact/damaged/destroyed top images; no anim or anim2 strip exists. Retain original hull plus four Tesla lightning emitters.'}
    for level in stronghold['levels']:
        level['attackIntervalSeconds'] = level['attackRateTicks']
        level['attackIntervalSource'] = 'client/scripts/GuardTower.as:GuardTowerTesla.fire schedules GLOBAL.Timestamp()+m_attackSpeed; source rate is seconds.'
    building = next(item for item in buildings if item['id'] == 140)
    cache = {}
    entries = []
    for level in building['levels']:
        descriptors = {relation: relationship_atlas(assets,building,level,relation,cache) for relation in RELATIONSHIPS}
        level['mapRoomRelationshipAnimations'] = descriptors
        entries.append({'building':140,'level':level['level'],'relationships':descriptors})
    building['mapRoomPresentation'] = {
        'source':'client/scripts/OutpostDefender.as',
        'damagedPolicy':'Source damaged/destroyed states have no animated light layer; use their original static composites.',
        'relationshipKeys':list(RELATIONSHIPS)}
    references = []
    for relative in SOURCE_FILES:
        destination = data/'Original/MapRoom3Art'/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/relative,destination)
        references.append({'path':relative,'sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
    report = {'schema':1,'building':140,'uniqueAtlases':len(cache),'entries':entries,'sourceReferences':references,
              'originalPixelsModified':False,'sourceDisplayHz':40,'sourceTickDivisor':1,
              'sourceCycle':'Copy current frame, then increment and wrap32. No random starting frame.',
              'notes':['These are original relationship-specific lights, not an invented attack sequence.',
                       'The five existing building levels share the same four source sheets.',
                       'Existing SourceAssets files are not rewritten.']}
    (data/'maproom-building-art.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[1])
    args=parser.parse_args()
    plugin=args.project/'Plugins/BackyardMMO';data=plugin/'Content/Data';assets=plugin/'SourceAssets'
    catalog=json.loads((data/'catalog.json').read_text())
    report=prepare_maproom_building_art(args.source,assets,data,catalog['buildings'])
    (data/'catalog.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2),encoding='utf-8')
    # The full prepare_assets pipeline normally handles this. Standalone mode
    # adds only these new atlas records without rewriting unrelated assets.
    from prepare_assets import object_path
    manifest=json.loads((data/'asset-manifest.json').read_text())
    entries={item['source']:item for item in manifest['assets']}
    for item in report['entries']:
        for descriptor in item['relationships'].values():
            relative=descriptor['texture'];path=assets/relative
            with Image.open(path) as image:
                entries[relative]={'source':relative,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'bytes':path.stat().st_size,'objectPath':object_path(relative),'importable':True,
                    'width':image.width,'height':image.height,'mode':image.mode}
    manifest['assets']=[entries[key] for key in sorted(entries)]
    (data/'asset-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps({'uniqueAtlases':report['uniqueAtlases'],'levels':len(report['entries']),
                      'framesPerAtlas':32,'originalImagesModified':0}))


if __name__=='__main__':main()
