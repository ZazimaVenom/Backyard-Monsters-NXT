"""Preserve original worker art and compose the five original mushroom variants.

No worker animation is invented: SPRITES selects one of thirty facing poses.
Mushrooms use SWF1695's frame1..5 top shapes and SWF1682's matching shadows.
"""
from __future__ import annotations
import hashlib
import json
import math
import shutil
from pathlib import Path
from PIL import Image, ImageChops, ImageOps
from prepare_combat import _building_atlas

# Original DefineShape bounds, extracted from assets.swf (source pixel units).
MUSHROOM_LAYERS = (
    (1684, (-27, -18.5), 1671, (-28, 0)),
    (1686, (-19, -5.5), 1673, (-18.5, -1)),
    (1688, (-20.5, -21.5), 1675, (-17, -5)),
    (1690, (-22.5, -12), 1677, (-21, -.5)),
    (1692, (-24.5, -20.5), 1679, (-17, 2.5)),
)

def prepare_workers(source: Path, assets: Path, data: Path, buildings: list) -> dict:
    # The general deduplicator omits shape517 because identical pixels already
    # exist elsewhere. Native source-symbol references require this exact path.
    for number in (517,):
        target=assets/f'source_client/shapes/{number}.png'
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/f'client/shapes/{number}.png',target)
    variants=[];inputs={}
    for frame,(top,top_origin,shadow,shadow_origin) in enumerate(MUSHROOM_LAYERS,1):
        layers={}
        for kind,number,origin in [('top',top,top_origin),('shadow',shadow,shadow_origin)]:
            relative=f'source_client/shapes/{number}.png';path=assets/relative
            raw=path.read_bytes();image=Image.open(path).convert('RGBA')
            inputs[relative]={'sha256':hashlib.sha256(raw).hexdigest(),'width':image.width,'height':image.height}
            output=relative
            if kind=='shadow':
                alpha=ImageChops.multiply(ImageOps.invert(image.convert('L')),image.getchannel('A'))
                image=Image.new('RGBA',image.size,(0,0,0,0));image.putalpha(alpha)
                output=f'derived/mushrooms/shadow_{frame}.png';dest=assets/output
                dest.parent.mkdir(parents=True,exist_ok=True);image.save(dest)
            layers[kind]={'texture':output,'pixelWidth':image.width,'pixelHeight':image.height,
                          'pivotX':-math.floor(origin[0]),'pivotY':-math.floor(origin[1]),
                          'sourceShape':number,'sourceBoundsOrigin':list(origin)}
        variants.append({'frame':frame,**layers})
    mushroom=next(b for b in buildings if b['id']==7)
    mushroom['mushroomPresentation']={'variants':variants,'source':'client/scripts/BMUSHROOM.as:PlaceB/HasWorker',
        'sourceTopSymbol':1695,'sourceShadowSymbol':1682,'sourceDisplayHz':40,
        'shakeCalls':62,'topShakePixels':2,'shadowShakePixels':1,
        'frameSelection':'Persisted frame1..5, independent of Shiny reward'}
    decorations=[];cache={}
    for building in buildings:
        if building['type']!='decoration':continue
        for level in building['levels']:
            if 'anim' not in level.get('imageLayers',{}):continue
            animation=_building_atlas(assets,building,level,'',cache)
            declared=int(level['imageLayers']['anim'][2])
            animation.update(mode='decoration',fps=20,sourceDisplayHz=40,sourceTickDivisor=2,
                source='client/scripts/BDECORATION.as:TickFast; BFOUNDATION.as:AnimFrame',
                idleFrames=[min(i,animation['frames']-1) for i in range(declared)],loop=True)
            level['animation']=animation
            decorations.append({'building':building['id'],'level':level['level'],'animation':animation})
    report={'schema':1,'originalPixelsModified':False,'inputs':inputs,'mushrooms':mushroom['mushroomPresentation'],
        'decorations':decorations,
        'worker':{'texture':'monsters/worker.png','frameWidth':27,'frameHeight':27,'directions':30,
            'normalRow':0,'boostedRow':1,'displayHz':40,'temporalFrameSequence':False,
            'movementSource':'client/scripts/WORKER.as:Move','selectorSource':'client/scripts/SPRITES.as:GetSprite',
            'movementAdaptation':'Deterministic host route, analytic acceleration, compact segment times; original directional artwork and hop easing'}}
    (data/'worker-art-provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report
