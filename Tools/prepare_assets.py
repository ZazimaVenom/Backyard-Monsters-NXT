#!/usr/bin/env python3
"""Read original AS literal catalogs without executing source and prepare fixed plugin assets.
Usage: python prepare_assets.py --source /path/to/backyard-monsters-refitted
Requires Python 3.10+, Pillow. Optional ffmpeg converts original music to PCM WAV.
"""
from __future__ import annotations
import argparse, ast, hashlib, html, json, math, re, shutil, subprocess
from pathlib import Path
from collections import Counter
from PIL import Image, ImageDraw, ImageOps
from prepare_terrain import GRASS_SOURCES, build_grass_tile
from prepare_ui import prepare_ui
from prepare_combat import prepare_combat
from prepare_experience import prepare_experience
from prepare_bunker import prepare_bunker
from prepare_workers import prepare_workers
from prepare_store_rules import prepare_store_rules
from prepare_maproom_art import prepare_maproom_art
from prepare_maproom_building_art import prepare_maproom_building_art
from prepare_maproom_rules import prepare as prepare_maproom_rules
from prepare_maproom_monsters import prepare as prepare_maproom_monsters

TOKEN = re.compile(r'\s+|//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|0x[0-9a-fA-F]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*|.', re.S)
class LiteralParser:
    """Restricted literal/arithmetic parser; identifiers are data, never evaluated."""
    def __init__(self, source):
        self.tokens = [x for x in TOKEN.findall(source) if not x.isspace() and not x.startswith('//') and not x.startswith('/*')]
        self.i=0
    def take(self, wanted=None):
        t=self.tokens[self.i];self.i+=1
        if wanted is not None and t!=wanted:raise ValueError(f'Expected {wanted!r}, got {t!r} near {self.tokens[max(0,self.i-8):self.i+8]}')
        return t
    def peek(self):return self.tokens[self.i]
    def value(self, min_precedence=0):
        t=self.take()
        if t in ['+','-']:
            n=self.value(3);v=n if t=='+' else -n
        elif t=='(':
            v=self.value();self.take(')')
        elif t=='[':
            v=[]
            while self.peek()!=']':
                v.append(self.value())
                if self.peek()!=',':break
                self.take(',')
            self.take(']')
        elif t=='{':
            v={}
            while self.peek()!='}':
                k=self.take();k=ast.literal_eval(k) if k[:1] in ['"',"'"] else k
                self.take(':');v[str(k)]=self.value()
                if self.peek()!=',':break
                self.take(',')
            self.take('}')
        elif t=='new':
            cls=self.take();self.take('(');args=[]
            while self.peek()!=')':
                args.append(self.value())
                if self.peek()!=',':break
                self.take(',')
            self.take(')')
            if cls=='SecNum':v=args[0]
            elif cls in ('Point','Rectangle'):v=args
            else:v={'constructor':cls,'arguments':args}
        elif t in ('true','false','null'):v={'true':True,'false':False,'null':None}[t]
        elif t[:1] in ('"',"'"):v=ast.literal_eval(t)
        elif t[:1].isdigit() or t.startswith('.'):
            v=int(t,16) if t.startswith('0x') else float(t) if any(c in t for c in '.eE') else int(t)
        else:v={'symbol':t}
        while self.i<len(self.tokens) and self.peek() in ['+','-','*','/']:
            op=self.peek();prec=1 if op in ['+','-'] else 2
            if prec<min_precedence:break
            self.take();rhs=self.value(prec+1)
            if isinstance(v,(int,float)) and isinstance(rhs,(int,float)):
                v={'+':lambda:v+rhs,'-':lambda:v-rhs,'*':lambda:v*rhs,'/':lambda:v/rhs}[op]()
            else:v={'expression':[v,op,rhs]}
        return v

def assignment(path, marker):
    txt=path.read_text(encoding='utf-8-sig');start=txt.index(marker)+len(marker)
    return LiteralParser(txt[start:]).value()
def safe_name(x):
    x=re.sub(r'[^A-Za-z0-9_]','_',x)
    return ('T_'+x) if x[:1].isdigit() else (x or 'Asset')
def object_path(rel):
    p=Path(rel);parts=[safe_name(x) for x in p.parts[:-1]]+[safe_name(p.stem)]
    return '/BackyardMMO/Art/'+'/'.join(parts)+'.'+parts[-1]
def pick(seq,index,default=0):
    if isinstance(seq,list) and seq:return seq[min(index,len(seq)-1)]
    return seq if isinstance(seq,(int,float)) else default

def monster_progression(creature_id, row):
    """Normalize the original active literal rules without executing ActionScript.

    CREATURES.GetProperty clamps each stat array independently to its final item.
    ACADEMY.StartMonsterUpgrade indexes trainingCosts by the CURRENT level minus
    one; the destination level is therefore the row index plus two. Unlock and
    research both charge resource 3 (Putty in the overworld, Sulfur in Inferno).
    Special class abilities and event unlocks are not inferred from these stats.
    """
    props = row.get('props', {})
    costs = row.get('trainingCosts', [])
    max_level = len(costs) + 1
    stat_fields = {
        'hp': ('health', 0), 'damage': ('damage', 0), 'speed': ('speed', 0),
        'range': ('range', 0), 'housing': ('cStorage', 0), 'goo': ('cResource', 0),
        'time': ('cTime', 0), 'targetGroup': ('targetGroup', 1),
        'attackDelayFrames': ('attackDelay', 0),
    }
    levels = []
    for level in range(1, max_level + 1):
        values = {'level': level}
        for field, (source_field, default) in stat_fields.items():
            value = pick(props.get(source_field, []), level - 1, default)
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value):
                raise ValueError(f'{creature_id} level {level}: invalid original {source_field}')
            values[field] = value
        levels.append(values)
    research = []
    resource_type = 'sulfur' if creature_id.startswith('IC') else 'putty'
    for index, cost in enumerate(costs):
        if not isinstance(cost, list) or len(cost) != 2 or any(
                not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) or v < 0 for v in cost):
            raise ValueError(f'{creature_id}: invalid original research row {index}')
        research.append({'fromLevel': index + 1, 'toLevel': index + 2,
                         'putty': cost[0], 'time': cost[1], 'requiredAcademyLevel': index + 1,
                         'sourceResourceType': resource_type})
    return {
        'levels': levels, 'research': research,
        'unlock': {'putty': row.get('resource', 0), 'time': row.get('time', 0),
                   'lockerLevel': row.get('level', 1),
                   'initiallyUnlocked': creature_id in ('C1', 'IC1'),
                   'sourceResourceType': resource_type},
        'sourceBlocked': bool(row.get('blocked', False)),
        'sourceFake': bool(row.get('fake', False)),
        'dependentMonster': row.get('dependent', ''),
        'sourcePage': row.get('page', 0), 'sourceOrder': row.get('order', 0),
        'progressionSource': {
            'stats': 'client/scripts/CREATURES.as:GetProperty; short arrays clamp to their final value',
            'unlock': 'client/scripts/CREATURELOCKER.as:Data/Start/Cancel/Tick',
            'research': 'client/scripts/ACADEMY.as:StartMonsterUpgrade/CancelMonsterUpgrade/FinishMonsterUpgrade',
            'building': 'client/scripts/BUILDING26.as:Upgrade/Recycle; one research job per Academy',
            'availability': 'client/scripts/CREATURELOCKERPOPUP.as:Update; blocked entries require separate source unlock systems',
        },
    }

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--plugin',type=Path,default=Path(__file__).resolve().parents[1]/'Plugins/BackyardMMO');ap.add_argument('--skip-audio',action='store_true');args=ap.parse_args()
    source=args.source.resolve();plugin=args.plugin.resolve();assets=plugin/'SourceAssets';data=plugin/'Content/Data';original=data/'Original';assets.mkdir(parents=True,exist_ok=True);original.mkdir(parents=True,exist_ok=True)
    scripts=source/'client/scripts';public=source/'server/public/assets'
    issues=[]
    for p in sorted(public.rglob('*')):
        if p.is_file() and p.suffix.lower() in ('.png','.jpg','.jpeg','.wav','.mp3'):
            target=assets/p.relative_to(public);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    # Unique decompiled embedded client images are preserved, with hashes to prevent duplicates.
    known={hashlib.sha256(p.read_bytes()).hexdigest() for p in public.rglob('*') if p.is_file() and p.suffix.lower() in ('.png','.jpg','.jpeg','.wav','.mp3')}
    for p in sorted((source/'client').rglob('*')):
        if p.suffix.lower() not in ('.png','.jpg','.jpeg','.mp3','.wav','.ttf'):continue
        digest=hashlib.sha256(p.read_bytes()).hexdigest()
        if digest not in known:
            target=assets/'source_client'/p.relative_to(source/'client');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target);known.add(digest)
    # Numeric references are parsed from their own source declarations, not guessed.
    constants={}
    for cls,relative in [('INFERNOQUAKETOWER','INFERNOQUAKETOWER.as'),('SiegeLab','com/monsters/siege/SiegeLab.as'),('SiegeFactory','com/monsters/siege/SiegeFactory.as')]:
        text=(scripts/relative).read_text(encoding='utf-8-sig');shutil.copy2(scripts/relative,original/(cls+'.as'))
        for match in re.finditer(r'public static const (\w+):(?:int|Number)\s*=\s*([0-9]+)\s*;',text):constants[cls+'.'+match[1]]=int(match[2])
    def resolve(value):
        if isinstance(value,dict):
            if 'symbol' in value and value['symbol'] in constants:return constants[value['symbol']]
            return {k:resolve(v) for k,v in value.items()}
        if isinstance(value,list):return [resolve(v) for v in value]
        return value
    raw={}
    for name in ('YARD_PROPS.as','INFERNOYARDPROPS.as','OUTPOST_YARD_PROPS.as'):
        text=(scripts/name).read_text(encoding='utf-8-sig');marker=re.search(r'_[A-Za-z_]+\s*:\s*Array\s*=\s*',text)
        if not marker:raise ValueError(f'Missing literal marker: {name}')
        raw[name]=LiteralParser(text[marker.end():]).value();shutil.copy2(scripts/name,original/name)
        (original/(Path(name).stem+'.json')).write_text(json.dumps(raw[name],indent=2),encoding='utf-8')
    creatures=assignment(scripts/'CREATURELOCKER.as','_mainCreatures = ')
    (original/'CREATURELOCKER.json').write_text(json.dumps(creatures,indent=2),encoding='utf-8')
    for name in ('CREATURELOCKER.as','CREATURES.as','ACADEMY.as','BUILDING8.as','BUILDING26.as','CREATURELOCKERPOPUP.as','ACADEMYPOPUP.as','HATCHERYPOPUP.as','HatcheryBase.as','HATCHERY.as','BUILDINGINFO.as','HOUSING.as','SPRITES.as','MAPBG.as','MAP.as','GRID.as','UI_TOP.as','UI_MENU.as','BUILDINGSPOPUP.as','BUILDINGBUTTON.as','CREATUREBUTTON.as','Button.as','StoneButton.as'):
        shutil.copy2(scripts/name,original/name)
    shutil.copy2(scripts/'com/monsters/display/SpriteData.as',original/'SpriteData.as')
    for name in ('LICENSE','README.md'):
        shutil.copy2(source/name,original/('SOURCE_'+name))
    lang=json.loads((source/'server/public/gamestage/assets/english.json').read_text(encoding='utf-8-sig'))
    (data/'english.json').write_text(json.dumps(lang,ensure_ascii=False,indent=2),encoding='utf-8')
    def localized(s):return lang.get(s,s.strip('#').replace('_',' ').title())
    def texture_for(row,level):
        image=row.get('imageData',{});keys=sorted(int(k) for k in image if k.isdigit());valid=[k for k in keys if k<=level]
        entry=image[str(max(valid))] if valid else {};top=entry.get('top');anim=entry.get('anim')
        path=(image.get('baseurl','')+top[0]) if top else ''
        if path and (assets/path).exists():return path,entry
        if anim:
            sheet=assets/(image.get('baseurl','')+anim[0])
            if sheet.exists():
                # Non-destructive extraction of a frame from the original atlas for top-less buildings.
                im=Image.open(sheet).convert('RGBA');rect=anim[1];w,h=int(rect[2]),int(rect[3]);rel=f'derived/buildings/{row["id"]}/level_{level}.png';dest=assets/rel;dest.parent.mkdir(parents=True,exist_ok=True);im.crop((0,0,w,h)).save(dest)
                return rel,entry
        # Original thumbnail is preferable to inventing art. Record this specific substitution.
        thumb=row.get('thumbImgData',{});keys=sorted(int(k) for k in thumb if k.isdigit());valid=[k for k in keys if k<=level]
        if valid:
            path=thumb.get('baseurl','')+thumb[str(max(valid))].get('img','')
            if (assets/path).is_file():
                issues.append(f'Building {row["id"]} level {level}: using original thumbnail because top layer is unavailable.');return path,entry
        return '',entry
    def composite(row, level, state=''):
        image=row.get('imageData',{});keys=sorted(int(k) for k in image if k.isdigit());valid=[k for k in keys if k<=level]
        if not valid:return None
        spec=image[str(max(valid))];layers=[]
        for label in ('shadow','top','anim','anim2','anim3'):
            key=label+state
            if key not in spec:continue
            item=spec[key];path=assets/(image.get('baseurl','')+item[0])
            if not path.exists():continue
            im=Image.open(path).convert('RGBA');rect=item[1];x,y=round(rect[0]),round(rect[1])
            if label.startswith('anim') and len(rect)>=4:im=im.crop((0,0,round(rect[2]),round(rect[3])))
            if label=='shadow':
                # Original Flash shadow is multiply blend; encode monochrome attenuation as alpha.
                alpha=ImageOps.invert(im.convert('L'));shadow=Image.new('RGBA',im.size,(0,0,0,0));shadow.putalpha(alpha);im=shadow
            layers.append((im,x,y))
        if not layers:return None
        left=min(x for im,x,y in layers);top=min(y for im,x,y in layers);right=max(x+im.width for im,x,y in layers);bottom=max(y+im.height for im,x,y in layers)
        out=Image.new('RGBA',(right-left,bottom-top),(0,0,0,0))
        for im,x,y in layers:out.alpha_composite(im,(x-left,y-top))
        rel=f'derived/buildings/composites/{row["id"]}_L{level}_{state or "normal"}.png';dest=assets/rel;dest.parent.mkdir(parents=True,exist_ok=True);out.save(dest)
        return {'texture':rel,'pixelWidth':out.width,'pixelHeight':out.height,'pivotX':-left,'pivotY':-top,'method':'original layer offsets; animation first frame; monochrome multiply shadow converted to alpha'}
    class_paths={p.stem:p for p in scripts.rglob('*.as')}
    def footprint_metadata(row):
        cls=row.get('cls',{}).get('symbol','') if isinstance(row.get('cls'),dict) else ''
        p=class_paths.get(cls)
        if not p:return {}
        code=p.read_text(encoding='utf-8-sig');m=re.search(r'_footprint\s*=\s*([^;]+);',code)
        if not m:return {'classSource':p.relative_to(source).as_posix()}
        expression=m[1].strip();literal=expression.rsplit(' : ',1)[-1] if '?' in expression else expression
        result={'classSource':p.relative_to(source).as_posix(),'originalFootprintExpression':expression}
        try:
            rects=LiteralParser(literal).value()
            if isinstance(rects,list) and all(isinstance(r,list) and len(r)==4 and all(isinstance(x,(int,float)) for x in r) for r in rects):
                result['collisionFootprints']=rects
                result['collisionFootprintWidth']=max(r[0]+r[2] for r in rects)-min(r[0] for r in rects)
                result['collisionFootprintHeight']=max(r[1]+r[3] for r in rects)-min(r[1] for r in rects)
        except (ValueError,TypeError,IndexError):pass
        return result
    buildings=[]
    for row in raw['YARD_PROPS.as']:
        if not isinstance(row,dict) or not isinstance(row.get('id'),int):continue
        levels=[]
        for i,cost in enumerate(row.get('costs',[])):
            if not isinstance(cost,dict):continue
            stats=pick(row.get('stats',[]),i,{})
            if not isinstance(stats,dict):stats={}
            tex,layer=texture_for(row,i+1)
            lv={'level':i+1,'twigs':cost.get('r1',0),'pebbles':cost.get('r2',0),'putty':cost.get('r3',0),'goo':cost.get('r4',0),'time':cost.get('time',0),'hp':pick(row.get('hp',[]),i,0),'production':pick(row.get('produce',[]),i,0),'productionCycleSeconds':pick(row.get('cycleTime',[]),i,10),'capacity':pick(row.get('capacity',[]),i,0),'damage':stats.get('damage',0),'range':stats.get('range',0),'attackRateTicks':stats.get('rate',0),'attackRateFrames':stats.get('rate',0),'attackIntervalSeconds':stats.get('rate',0)/30.0,'splash':stats.get('splash',0),'repairTime':pick(row.get('repairTime',[]),i,0),'requirements':resolve(cost.get('re',[])),'texture':tex}
            if layer:lv['imageLayers']=layer
            for state,label in (('','composite'),('damaged','damagedComposite'),('destroyed','destroyedComposite')):
                comp=composite(row,i+1,state)
                if comp:lv[label]=comp
            if 'composite' in lv:lv['compositeTexture']=lv['composite']['texture']
            levels.append(lv)
        tex,_=texture_for(row,1)
        entry={'id':row['id'],'name':localized(row.get('name','Unknown')),'type':row.get('type','special'),'size':row.get('size',100),'texture':tex,'levels':levels,'quantityByTownHall':row.get('quantity',[]),'buildStatus':row.get('buildStatus',0),'group':row.get('group',0),'attackgroup':row.get('attackgroup',0),'description':localized(row.get('description','')),'source':'client/scripts/YARD_PROPS.as','imageBaseURL':row.get('imageData',{}).get('baseurl','')}
        entry.update(footprint_metadata(row))
        menu_names=row.get('buildingbuttons') or [str(row['id'])]
        menu_path='buildingbuttons/'+str(menu_names[0])+'.jpg'
        if not (assets/menu_path).is_file() and (assets/f'buildingbuttons/{row["id"]}.1.jpg').is_file():menu_path=f'buildingbuttons/{row["id"]}.1.jpg'
        if (assets/menu_path).is_file():entry['menuTexture']=menu_path
        thumbs=row.get('thumbImgData',{});thumb=thumbs.get('1',{}).get('img','')
        thumbnail_path=thumbs.get('baseurl','')+thumb
        if thumb and (assets/thumbnail_path).is_file():entry['thumbnailTexture']=thumbnail_path
        if not tex:issues.append(f'Building {row["id"]}: no image found; runtime must hide unavailable items rather than silently invent art.')
        buildings.append(entry)
    # Preserve all sprite descriptors and derive actual atlas row/column counts from the images.
    sprite_text=(scripts/'SPRITES.as').read_text(encoding='utf-8-sig');sprites={}
    for m in re.finditer(r'_sprites\.([\w]+)\s*=\s*new SpriteData\("([^"]+)",\s*([\d.]+),\s*([\d.]+),\s*([^,;]+),\s*([^);]+)\)',sprite_text):
        sid,path,w,h,px,py=m.groups();w,h=float(w),float(h)
        if sid=='worker' and path=='monsters/inferno_worker.v2.png':sid='inferno_worker'
        if not (assets/path).exists():issues.append(f'Sprite {sid}: missing source {path}');continue
        im=Image.open(assets/path);source_w,source_h=w,h
        # Some original strips were trimmed shorter than their declared frame rectangle.
        # Keep source dimensions explicitly but never sample beyond the actual runtime image.
        w,h=min(w,float(im.width)),min(h,float(im.height))
        cols=max(1,int(im.width/w+.001));rows=max(1,int(im.height/h+.001))
        if source_w!=w or source_h!=h or abs(cols*w-im.width)>1.0:issues.append(f'Sprite {sid}: original descriptor {source_w}x{source_h} differs from atlas {im.width}x{im.height}; originalFrameWidth/Height preserve declarations, runtime frame size is bounded to the image.')
        def pivot(s):
            s=s.replace('SpriteData.FUBAR_X','26').replace('SpriteData.FUBAR_Y','36')
            return LiteralParser(s).value()
        sprites[sid]={'texture':path,'textureWidth':im.width,'textureHeight':im.height,'frameWidth':w,'frameHeight':h,'originalFrameWidth':source_w,'originalFrameHeight':source_h,'columns':cols,'rows':rows,'frames':cols*rows,'directions':cols,'framesPerDirection':rows,'pivotX':pivot(px),'pivotY':pivot(py),'layout':'columns_are_facing_directions_rows_are_animation_states','fps':7.5,'fpsStatus':'presentation default; original uses frame/state-specific pacing in SPRITES.GetSprite'}
        # Rows are states, not a blanket animation range. These explicit source branches avoid
        # e.g. alternating Brain's normal row with its invisible row.
        idle_row=1 if sid=='C19' else 0
        start,count={'C14':(0,3),'C16':(0,3),'C19':(1,5),'IC1':(1,2),'IC3':(1,8),'IC5':(1,6)}.get(sid,(idle_row,1))
        directions=16 if sid.startswith('G') else 32 if sid in ('C14','C15','C16','IC1','rocket') else 30
        sprites[sid].update(idleRow=min(idle_row,rows-1),movingStartRow=min(start,rows-1),movingFrames=min(count,max(1,rows-start)),sourceDirections=directions,directions=min(directions,cols),animationStateSource='client/scripts/SPRITES.as:GetSprite',sourceFacingOffsetDegrees=-135 if sid.startswith('G') else -90,sourceFacingOffsetSource='MonsterBase.render atan2(yd,xd)-90; ChampionBase applies another -45')
        rel=f'derived/monsters/{sid}_idle.png';dest=assets/rel;dest.parent.mkdir(parents=True,exist_ok=True);im.crop((0,round(h)*min(idle_row,rows-1),round(w),round(h)*(min(idle_row,rows-1)+1))).save(dest)
        sprites[sid]['idleTexture']=rel
    monsters=[]
    for cid,row in creatures.items():
        p=row.get('props',{});desc=sprites.get(cid,{})
        if not desc:issues.append(f'Monster {cid}: no descriptor in SPRITES.as')
        entry={'id':cid,'name':localized(row.get('name',cid)),'texture':desc.get('texture',''),'hp':pick(p.get('health',[]),0,0),'damage':pick(p.get('damage',[]),0,0),'speed':pick(p.get('speed',[]),0,0),'range':pick(p.get('range',[]),0,0),'housing':pick(p.get('cStorage',[]),0,0),'goo':pick(p.get('cResource',[]),0,0),'time':pick(p.get('cTime',[]),0,0),'requiredLevel':row.get('level',1),'targetGroup':pick(p.get('targetGroup',[]),0,1),'unlockResource':row.get('resource',0),'unlockTime':row.get('time',0),'unlockRequirements':row.get('unlock',[]),'trainingCosts':row.get('trainingCosts',[]),'sourceProps':p,'sprite':desc,'source':'client/scripts/CREATURELOCKER.as','realm':'inferno' if cid.startswith('IC') else 'overworld','description':localized(row.get('description',''))}
        entry.update(movement=row.get('movement','walk'),pathing=row.get('pathing',''),flying=row.get('movement') in ('fly','fly_low'),movementExplicit='movement' in row,attackDelayFrames=pick(p.get('attackDelay',[]),0,0))
        entry.update(monster_progression(cid, row))
        entry['descriptionPlain']=' '.join(re.sub(r'<[^>]*>',' ',html.unescape(entry['description'])).split())
        icon=f'monsters/{cid}-small.png'
        if (assets/icon).is_file():entry['menuTexture']=icon
        for field, suffix in [('portraitTexture', 'portrait.jpg'), ('mediumTexture', 'medium.jpg')]:
            image=f'monsters/{cid}-{suffix}'
            if (assets/image).is_file():entry[field]=image
        entry.update({k:desc[k] for k in ('textureWidth','textureHeight','frameWidth','frameHeight','originalFrameWidth','originalFrameHeight','columns','rows','frames','directions','framesPerDirection','fps') if k in desc})
        if 'range' not in p:entry['rangeStatus']='not in literal; specialized class governs original range, runtime melee fallback required'
        monsters.append(entry)
    music=[]
    if not args.skip_audio and shutil.which('ffmpeg'):
        for p in sorted(assets.rglob('*.mp3')):
            out=p.with_suffix('.wav')
            if not out.exists() or out.stat().st_mtime < p.stat().st_mtime:subprocess.run(['ffmpeg','-v','error','-y','-i',str(p),'-acodec','pcm_s16le','-ar','44100',str(out)],check=True)
            if p.parent==assets/'sounds/music':music.append(out.relative_to(assets).as_posix())
    music=[p.relative_to(assets).as_posix() for p in sorted((assets/'sounds/music').glob('*.wav'))]
    # MAPBG.MakeTile blends repeated rectangular swatches through soft alpha masks.
    # They are not diamond tiles and must never be pasted in alternating rows.
    dest=assets/'derived/terrain/grass_blended_tile.png';dest.parent.mkdir(parents=True,exist_ok=True)
    build_grass_tile(assets,seed=0).save(dest)
    obsolete=assets/'derived/terrain/grass_field.png'
    if obsolete.exists():obsolete.unlink()
    # A transparent placement outline leaves the actual ground visible. It is UI,
    # not replacement terrain or an invented game-world texture.
    outline=Image.new('RGBA',(128,128),(255,255,255,0))
    ImageDraw.Draw(outline).rectangle((1,1,126,126),outline=(255,255,255,255),width=2)
    outline_path=assets/'derived/ui/placement_outline.png';outline_path.parent.mkdir(parents=True,exist_ok=True);outline.save(outline_path)
    diamond=Image.new('RGBA',(256,128),(255,255,255,0))
    ImageDraw.Draw(diamond).line([(128,1),(254,64),(128,126),(1,64),(128,1)],fill=(255,255,255,255),width=2)
    diamond.save(assets/'derived/ui/placement_diamond.png')
    # Interaction masks, not replacement game artwork. The broad translucent
    # footprint and two-pass outline remain visible over original grass.
    diamond_points=[(128,4),(251,64),(128,123),(4,64)]
    diamond_fill=Image.new('RGBA',(256,128),(255,255,255,0))
    ImageDraw.Draw(diamond_fill).polygon(diamond_points,fill=(255,255,255,55))
    diamond_fill.save(assets/'derived/ui/placement_diamond_fill.png')
    diamond_outline=Image.new('RGBA',(256,128),(255,255,255,0))
    ImageDraw.Draw(diamond_outline).polygon(diamond_points,outline=(255,255,255,255),width=8)
    diamond_outline.save(assets/'derived/ui/placement_diamond_outline.png')
    # Use the complete embedded font variants; some SWF font exports contain
    # only a handful of glyphs. RuntimeDependencies stages these into the game.
    font_dir=plugin/'Content/UI/Fonts';font_dir.mkdir(parents=True,exist_ok=True)
    for source_name,target_name in [('31_Groboldov.ttf','GROBOLD.ttf'),('4_Verdana.ttf','Verdana.ttf'),('2960_Verdana Bold.ttf','VerdanaBold.ttf')]:
        shutil.copy2(assets/'source_client/fonts'/source_name,font_dir/target_name)
    prepare_ui(assets,data)
    prepare_experience(source,plugin)
    combat=prepare_combat(source,assets,data,buildings,monsters,sprites)
    prepare_bunker(source,assets,data,buildings)
    prepare_workers(source,assets,data,buildings)
    prepare_store_rules(source,assets,data,buildings,raw['YARD_PROPS.as'])
    prepare_maproom_art(source,assets,data)
    prepare_maproom_building_art(source,assets,data,buildings)
    prepare_maproom_rules(source,plugin.parents[1])
    prepare_maproom_monsters(source,data)
    issues=[issue for issue in issues if not issue.startswith('Building 7 level 1: using original thumbnail')]
    issues.append('Mushroom thumbnail is UI-only; live yard rendering uses five original SWF top variants with their matching multiply shadows.')
    catalog={'schema':1,'sourceVersion':'128.5690 (original source README)','sourceCatalog':'YARD_PROPS.as + CREATURELOCKER.as; original raw catalogs retained in Content/Data/Original','buildings':buildings,'monsters':monsters,'sprites':sprites,'terrain':{'grassField':'derived/terrain/grass_blended_tile.png','grass':'yardbg/grass/2174_isograss1_isograss1.png','grassAlternates':list(GRASS_SOURCES),'placementOutline':'derived/ui/placement_outline.png','worldMap':'worldmap/background.jpg','composition':{'source':'client/scripts/MAPBG.as:MakeTile','seed':0,'tileWidth':1000,'tileHeight':500,'swatchWidth':200,'swatchHeight':100,'octaves':2,'stitch':True,'fractalNoise':False,'seedPolicy':'Prepared original default base seed; per-yard original baseseed selection is not implemented.'}},'music':music,'conversionNotes':sorted(set(issues)),'unimplementedOriginalSystems':['Complete original specialized creep AI / projectile behavior','Inferno and outpost campaign logic','Champions, siege weapons, events, quests and monetization','State-specific animation frame selection; raw sprite control preserved']}
    catalog['combatTiming']=combat['timing']
    catalog['effects']=combat['effects']
    catalog['combatSounds']=combat['sounds']
    catalog['unimplementedOriginalSystems'].remove('State-specific animation frame selection; raw sprite control preserved')
    catalog['unimplementedOriginalSystems'].append('Monster attack/death sequences absent from original atlases cannot be fabricated; explicit available source row states and original effects are catalogued')
    (data/'catalog.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2),encoding='utf-8')
    manifest=[];collisions={}
    for p in sorted(assets.rglob('*')):
        if not p.is_file():continue
        rel=p.relative_to(assets).as_posix();entry={'source':rel,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size,'objectPath':object_path(rel),'importable':p.suffix.lower() in ('.png','.jpg','.jpeg','.wav')}
        if entry['importable']:
            key=entry['objectPath'].lower()
            if key in collisions:raise ValueError(f'Unreal asset name collision: {rel} and {collisions[key]}')
            collisions[key]=rel
        if p.suffix.lower() in ('.png','.jpg','.jpeg'):
            with Image.open(p) as im:entry.update(width=im.width,height=im.height,mode=im.mode)
        manifest.append(entry)
    (data/'asset-manifest.json').write_text(json.dumps({'schema':1,'root':'SourceAssets','assets':manifest},indent=2),encoding='utf-8')
    by_hash={}
    for entry in manifest:by_hash.setdefault(entry['sha256'],entry['source'])
    source_index=[]
    for p in sorted(source.rglob('*')):
        if not p.is_file() or p.suffix.lower() not in ('.png','.jpg','.jpeg','.mp3','.wav','.ttf'):continue
        sha=hashlib.sha256(p.read_bytes()).hexdigest()
        source_index.append({'original':p.relative_to(source).as_posix(),'sha256':sha,'prepared':by_hash.get(sha),'status':'preserved' if sha in by_hash else 'missing'})
    (data/'source-index.json').write_text(json.dumps({'schema':1,'files':source_index},indent=2),encoding='utf-8')
    summary={'assetFiles':len(manifest),'importableAssets':sum(x['importable'] for x in manifest),'sourceBytes':sum(x['bytes'] for x in manifest),'buildings':len(buildings),'buildingLevels':sum(len(x['levels']) for x in buildings),'monsters':len(monsters),'sprites':len(sprites),'conversionIssues':len(set(issues)),'originalVisualAudioFontFiles':len(source_index),'originalAssetsMapped':sum(x['status']=='preserved' for x in source_index),'missingBuildingTextures':[x['id'] for x in buildings if not x['texture']],'originalCatalogCounts':{k:len(v) for k,v in raw.items()}}
    (data/'conversion-report.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    # Review contact sheet is an analysis aid; every thumbnail is unmodified original/atlas-frame art.
    selected=[]
    for row in buildings[:26]:
        if row['texture']:
            tex=(row['levels'][0].get('compositeTexture') if row['levels'] else None) or row['texture'];selected.append((f'{row["id"]}: {row["name"]}',assets/tex))
    for cid,desc in list(sprites.items())[:20]:selected.append((cid,assets/desc['idleTexture']))
    cellw,cellh=160,150;sheet=Image.new('RGB',(cellw*6,cellh*math.ceil(len(selected)/6)),(36,48,40));draw=ImageDraw.Draw(sheet)
    for i,(label,p) in enumerate(selected):
        im=Image.open(p).convert('RGBA');im.thumbnail((150,120));x=(i%6)*cellw+(cellw-im.width)//2;y=(i//6)*cellh;sheet.paste(im,(x,y),im);draw.text(((i%6)*cellw+6,y+126),label[:23],fill='white')
    sheet.save(data/'asset-contact-sheet.jpg',quality=90)
    print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
