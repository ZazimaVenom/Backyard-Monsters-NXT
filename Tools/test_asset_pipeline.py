#!/usr/bin/env python3
"""Verify prepared source assets, safe literal parsing and catalog invariants without Unreal."""
import hashlib,json,math,re,unittest
from pathlib import Path
from PIL import Image, ImageStat, ImageChops, ImageOps
from prepare_assets import LiteralParser,object_path
from prepare_terrain import GRASS_SOURCES, TerrainNoise, _next_random
ROOT=Path(__file__).resolve().parents[1]
PLUGIN=ROOT/'Plugins/BackyardMMO'
DATA=PLUGIN/'Content/Data'
class AssetPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog=json.loads((DATA/'catalog.json').read_text())
        cls.manifest=json.loads((DATA/'asset-manifest.json').read_text())['assets']
    def test_safe_parser_numeric_expressions(self):
        p=LiteralParser('{"cost":new SecNum(60*60*2),"rect":new Rectangle(-4,10,23,33),"symbol":SiegeLab.ID,"flags":[true,false,null]}')
        self.assertEqual(p.value(),{'cost':7200,'rect':[-4,10,23,33],'symbol':{'symbol':'SiegeLab.ID'},'flags':[True,False,None]})
    def test_bunker_door_preserves_every_original_cell_and_static_damage(self):
        bunker=next(b for b in self.catalog['buildings'] if b['id']==22)
        descriptors=[level['animation'] for level in bunker['levels']]
        self.assertEqual(len(descriptors),5)
        self.assertEqual(len({d['texture'] for d in descriptors}),1)
        descriptor=descriptors[0]
        self.assertEqual((descriptor['mode'],descriptor['sourceDisplayHz'],descriptor['sourcePhaseMaximum']),('bunker_door',40,15))
        self.assertEqual((descriptor['frames'],descriptor['closedFrame'],descriptor['openFrame']),(15,0,14))
        self.assertEqual((descriptor['textureWidth'],descriptor['textureHeight'],descriptor['frameWidth'],descriptor['frameHeight']),(1104,200,138,100))
        self.assertEqual((descriptor['pivotX'],descriptor['pivotY'],descriptor['columns'],descriptor['rows']),(66,15,8,2))
        root=PLUGIN/'SourceAssets'
        with Image.open(root/'buildings/bunker/anim.1.png') as source, Image.open(root/'buildings/bunker/shadow.1.jpg') as shadow_source, Image.open(root/descriptor['texture']) as atlas:
            self.assertEqual(source.size,(1350,83))
            shadow=Image.new('RGBA',shadow_source.size,(0,0,0,0))
            shadow.putalpha(ImageOps.invert(shadow_source.convert('L')))
            hashes=set()
            for index in range(15):
                expected=Image.new('RGBA',(138,100),(0,0,0,0))
                expected.alpha_composite(shadow,(0,25))
                expected.alpha_composite(source.crop((index*90,0,(index+1)*90,83)).convert('RGBA'),(20,0))
                x,y=index%8*138,index//8*100
                actual=atlas.crop((x,y,x+138,y+100)).convert('RGBA')
                self.assertEqual(actual.tobytes(),expected.tobytes(),f'Original Bunker cell{index} or layer offset changed')
                hashes.add(hashlib.sha256(actual.tobytes()).hexdigest())
            self.assertGreater(len(hashes),10)
            self.assertIsNone(atlas.crop((7*138,100,8*138,200)).getbbox(),'Unused sixteenth atlas slot must stay transparent')
        for level in bunker['levels']:
            self.assertNotIn('damaged',level['animation'])
            self.assertTrue((root/level['damagedComposite']['texture']).is_file())
            self.assertTrue((root/level['destroyedComposite']['texture']).is_file())
        self.assertEqual(self.catalog['combatSounds']['bunkerdoor'],'sounds/attacksounds/bunkerdoor.wav')
    def test_original_asset_hashes_are_all_preserved(self):
        source_index=json.loads((DATA/'source-index.json').read_text())['files']
        self.assertEqual(len(source_index),6273)
        manifest_by_path={x['source']:x for x in self.manifest}
        for x in source_index:
            self.assertEqual(x['status'],'preserved',x['original'])
            self.assertEqual(manifest_by_path[x['prepared']]['sha256'],x['sha256'])
    def test_manifest_files_sizes_hashes_and_collision_free_paths(self):
        names=set()
        for x in self.manifest:
            p=PLUGIN/'SourceAssets'/x['source']
            self.assertTrue(p.is_file(),str(p));self.assertEqual(p.stat().st_size,x['bytes'])
            self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),x['sha256'])
            self.assertEqual(x['objectPath'],object_path(x['source']))
            if x['importable']:
                self.assertNotIn(x['objectPath'].lower(),names);names.add(x['objectPath'].lower())
    def test_original_known_values_and_numeric_requirements(self):
        buildings={x['id']:x for x in self.catalog['buildings']};monsters={x['id']:x for x in self.catalog['monsters']}
        self.assertEqual(len(buildings),140);self.assertEqual(len(monsters),28)
        twig=buildings[1]['levels'][0]
        self.assertEqual((twig['pebbles'],twig['time'],twig['hp'],twig['production'],twig['productionCycleSeconds']),(750,15,500,2,10))
        self.assertEqual((monsters['C1']['hp'],monsters['C1']['damage'],monsters['C1']['goo'],monsters['C1']['time']),(200,60,250,15))
        self.assertLess(monsters['C15']['damage'],0) # Original healer must remain a healer.
        self.assertEqual(buildings[129]['levels'][0]['requirements'][0][0],999)
        for b in buildings.values():
            for lv in b['levels']:
                for k in ('twigs','pebbles','putty','goo','time','hp','production','capacity','damage','range'):
                    self.assertIsInstance(lv[k],(int,float),(b['id'],k));self.assertTrue(math.isfinite(lv[k]));self.assertGreaterEqual(lv[k],0)
                for req in lv['requirements']:
                    self.assertEqual(len(req),3)
                    for n in req:self.assertIsInstance(n,(int,float))
    def test_monster_progression_matches_original_level_and_research_rules(self):
        monsters={x['id']:x for x in self.catalog['monsters']}
        original=json.loads((DATA/'Original/CREATURELOCKER.json').read_text())
        # Independent numerical examples from CREATURELOCKER + CREATURES and
        # ACADEMY. Different length arrays must clamp separately, not reset to L1.
        pokey=monsters['C1']
        self.assertEqual((pokey['levels'][5]['hp'],pokey['levels'][5]['damage'],pokey['levels'][5]['goo'],pokey['levels'][5]['time'],pokey['levels'][5]['housing']),(300,85,1250,5,7))
        self.assertTrue(pokey['unlock']['initiallyUnlocked'])
        self.assertFalse(monsters['C2']['unlock']['initiallyUnlocked'])
        self.assertEqual((monsters['C2']['unlock']['putty'],monsters['C2']['unlock']['time'],monsters['C2']['unlock']['lockerLevel']),(8000,3600,1))
        self.assertEqual([row['time'] for row in monsters['C2']['levels']],[15,16,16,16,16,16])
        self.assertEqual([row['hp'] for row in monsters['C3']['levels']],[150]*6)
        self.assertEqual((pokey['research'][0]['fromLevel'],pokey['research'][0]['toLevel'],pokey['research'][0]['putty'],pokey['research'][0]['time'],pokey['research'][0]['requiredAcademyLevel']),(1,2,4000,7200,1))
        self.assertEqual((pokey['research'][4]['toLevel'],pokey['research'][4]['putty'],pokey['research'][4]['time'],pokey['research'][4]['requiredAcademyLevel']),(6,22000,43200,5))
        self.assertTrue(monsters['C17']['sourceBlocked'])
        self.assertTrue(monsters['C18']['sourceFake'])
        self.assertEqual(monsters['C18']['dependentMonster'],'C17')
        self.assertTrue(monsters['C19']['sourceBlocked'])
        for cid,monster in monsters.items():
            raw=original[cid]
            self.assertEqual(len(monster['levels']),len(raw.get('trainingCosts',[]))+1,cid)
            self.assertEqual(monster['sourceBlocked'],bool(raw.get('blocked',False)),cid)
            self.assertEqual(monster['sourceFake'],bool(raw.get('fake',False)),cid)
            for current,price in enumerate(raw.get('trainingCosts',[]),1):
                research=monster['research'][current-1]
                self.assertEqual((research['fromLevel'],research['toLevel'],research['putty'],research['time'],research['requiredAcademyLevel']),(current,current+1,price[0],price[1],current),cid)
            for row in monster['levels']:
                for field,source in [('hp','health'),('damage','damage'),('speed','speed'),('goo','cResource'),('housing','cStorage'),('time','cTime')]:
                    values=raw['props'][source]
                    self.assertEqual(row[field],values[min(row['level'],len(values))-1],(cid,row['level'],field))
            self.assertNotRegex(monster['descriptionPlain'],r'<[^>]*>')
        self.assertTrue(all(row['damage']<0 for row in monsters['C15']['levels']))
        self.assertEqual(monsters['C1']['portraitTexture'],'monsters/C1-portrait.jpg')
        self.assertEqual(monsters['C1']['mediumTexture'],'monsters/C1-medium.jpg')
        for name in ('CREATURES.as','ACADEMY.as','BUILDING8.as','BUILDING26.as','CREATURELOCKERPOPUP.as','ACADEMYPOPUP.as','HATCHERYPOPUP.as'):
            self.assertTrue((DATA/'Original'/name).is_file(),name)
    def test_catalog_textures_and_atlas_dimensions(self):
        for b in self.catalog['buildings']:
            for key in ('menuTexture','thumbnailTexture'):
                if b.get(key):self.assertTrue((PLUGIN/'SourceAssets'/b[key]).is_file(),b[key])
            for lv in b['levels']:
                for key in ('texture','compositeTexture'):
                    if lv.get(key):self.assertTrue((PLUGIN/'SourceAssets'/lv[key]).is_file(),lv[key])
                for key in ('composite','damagedComposite','destroyedComposite'):
                    if key in lv:
                        d=lv[key]
                        with Image.open(PLUGIN/'SourceAssets'/d['texture']) as im:self.assertEqual(im.size,(d['pixelWidth'],d['pixelHeight']))
        c11=self.catalog['sprites']['C11']
        self.assertEqual((c11['frameHeight'],c11['originalFrameHeight']),(34,35))
        for sprite in self.catalog['sprites'].values():
            with Image.open(PLUGIN/'SourceAssets'/sprite['texture']) as im:
                self.assertLessEqual(sprite['frameWidth'],im.width)
                self.assertLessEqual(sprite['frameHeight'],im.height)
        with Image.open(PLUGIN/'SourceAssets'/c11['idleTexture']) as im:self.assertEqual(im.size,(48,34))
        for m in self.catalog['monsters']:
            for key in ('menuTexture','portraitTexture','mediumTexture'):
                if m.get(key):self.assertTrue((PLUGIN/'SourceAssets'/m[key]).is_file())
            self.assertGreaterEqual(m['rows'],1);self.assertGreaterEqual(m['columns'],1)
            self.assertTrue((PLUGIN/'SourceAssets'/m['texture']).is_file())
        with Image.open(PLUGIN/'SourceAssets'/self.catalog['terrain']['grassField']) as im:
            self.assertEqual(im.size,(1000,500));self.assertEqual(im.getchannel('A').getextrema(),(255,255))
    def test_atlas_image_dimensions_preserve_actual_source_padding(self):
        # UV scale must be frameWidth/textureWidth, not 1/columns or a transient
        # Unreal texture resource size. Several original sheets have padding.
        descriptors=list(self.catalog['sprites'].values())+list(self.catalog['monsters'])
        descriptors.extend(self.catalog['effects'].values())
        for building in self.catalog['buildings']:
            for level in building['levels']:
                if 'animation' in level:
                    descriptors.append(level['animation'])
                    if 'damaged' in level['animation']:
                        descriptors.append(level['animation']['damaged'])
        for descriptor in descriptors:
            with Image.open(PLUGIN/'SourceAssets'/descriptor['texture']) as image:
                self.assertEqual((descriptor['textureWidth'],descriptor['textureHeight']),image.size,descriptor['texture'])
                # Metadata dimensions describe the whole image while the
                # pre-existing valid frame/row counts still exclude padding.
                self.assertLessEqual(descriptor['columns']*descriptor['frameWidth'],image.width+1e-8)
                self.assertLessEqual(descriptor['rows']*descriptor['frameHeight'],image.height+1e-8)
        sprites=self.catalog['sprites']
        for sid,width,height,covered_width,covered_height in [
                ('C13',1280,27,1280,26), ('C18',960,116,960,100),
                ('IC2',1933,46,1932,46), ('IC6',1718,39,1710,39)]:
            sprite=sprites[sid]
            self.assertEqual((sprite['textureWidth'],sprite['textureHeight']),(width,height),sid)
            self.assertAlmostEqual(sprite['columns']*sprite['frameWidth'],covered_width,msg=sid)
            self.assertAlmostEqual(sprite['rows']*sprite['frameHeight'],covered_height,msg=sid)
        # A large atlas uses atlas dimensions here, not its per-frame display size.
        tesla=next(b for b in self.catalog['buildings'] if b['id']==25)['levels'][0]['animation']
        self.assertEqual((tesla['textureWidth'],tesla['textureHeight']),(792,910))
        self.assertEqual((tesla['frameWidth'],tesla['frameHeight']),(99,130))
    def test_every_original_pokey_direction_contains_a_complete_pose(self):
        sprite=self.catalog['sprites']['C1']
        path=PLUGIN/'SourceAssets'/sprite['texture']
        # The source file is unchanged; the reported vertical-strip failure is
        # a sampling bug, not missing/replaced pixels in individual directions.
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                         '9ea59db10c783f6d24f3a1559ad7e03e94eff8466d9a2de91d17c1d642248de6')
        self.assertEqual((sprite['textureWidth'],sprite['textureHeight'],sprite['columns'],sprite['rows']),(720,21,30,1))
        with Image.open(path) as image:
            for facing in range(30):
                alpha=image.crop((facing*24,0,(facing+1)*24,21)).getchannel('A')
                bounds=alpha.getbbox();self.assertIsNotNone(bounds,facing)
                self.assertGreaterEqual(bounds[2]-bounds[0],22,facing)
                self.assertGreaterEqual(bounds[3]-bounds[1],19,facing)
                self.assertGreaterEqual(sum(alpha.histogram()[1:]),270,facing)
    def test_terrain_noise_reference_sequence_and_stitched_edges(self):
        state=1
        for _ in range(10000):state=_next_random(state)
        self.assertEqual(state,1043618065) # Published Park-Miller reference value.
        for layer in range(2,8):
            noise=TerrainNoise(1+layer,50*layer,25*layer)
            samples=[noise.sample(x,y) for x,y in ((37,91),(199,99),(641,342),(997,497))]
            self.assertGreater(max(samples)-min(samples),.01)
            self.assertEqual(samples,[TerrainNoise(1+layer,50*layer,25*layer).sample(x,y) for x,y in ((37,91),(199,99),(641,342),(997,497))])
            self.assertNotEqual(samples,[TerrainNoise(100+layer,50*layer,25*layer).sample(x,y) for x,y in ((37,91),(199,99),(641,342),(997,497))])
            for x,y in ((37,91),(199,99),(641,342),(997,497)):
                self.assertAlmostEqual(noise.sample(x,y),noise.sample(x+1000,y+500),places=9)
                # Test either side of the repeat seam, not only modulo equality.
                self.assertLess(abs(noise.sample(.001,y)-noise.sample(999.999,y)),.0001)
                self.assertLess(abs(noise.sample(x,.001)-noise.sample(x,499.999)),.0001)
    def test_terrain_has_blended_layers_without_rectangular_boundaries(self):
        self.assertEqual(self.catalog['terrain']['grassAlternates'],list(GRASS_SOURCES))
        with Image.open(PLUGIN/'SourceAssets'/self.catalog['terrain']['grassField']) as source:
            im=source.convert('RGB');pixels=im.load()
            # The faulty staggered composer inserted sharp full-row transitions
            # at every half-swatch height. Those boundaries must be ordinary grass.
            row_differences=[sum(abs(pixels[x,y][c]-pixels[x,y-1][c]) for x in range(im.width) for c in range(3))/(im.width*3) for y in range(1,im.height)]
            average=sum(row_differences)/len(row_differences)
            self.assertGreater(average,1) # Preserve real texture detail, not a flat fill.
            for y in range(50,im.height,50):self.assertLess(row_differences[y-1],average*1.5)
            # Original swatches are textured and are not pixel-identical at their
            # edges; retain that art, but reject a high-contrast repeat boundary.
            vertical=sum(abs(pixels[0,y][c]-pixels[im.width-1,y][c]) for y in range(im.height) for c in range(3))/(im.height*3)
            horizontal=sum(abs(pixels[x,0][c]-pixels[x,im.height-1][c]) for x in range(im.width) for c in range(3))/(im.width*3)
            self.assertLess(max(vertical,horizontal),average*2)
    def test_placement_outline_keeps_terrain_visible(self):
        with Image.open(PLUGIN/'SourceAssets'/self.catalog['terrain']['placementOutline']) as im:
            alpha=im.getchannel('A')
            self.assertEqual(alpha.crop((4,4,124,124)).getextrema(),(0,0))
            self.assertEqual(alpha.getextrema(),(0,255))
            self.assertLess(ImageStat.Stat(alpha).mean[0],25)
        with Image.open(PLUGIN/'SourceAssets/derived/ui/placement_diamond.png') as im:
            self.assertEqual(im.size,(256,128))
            self.assertEqual(im.getpixel((128,64))[3],0)
            self.assertEqual(im.getpixel((128,1))[3],255)
            self.assertEqual(im.getpixel((254,64))[3],255)
        with Image.open(PLUGIN/'SourceAssets/derived/ui/placement_diamond_fill.png') as im:
            self.assertEqual(im.size,(256,128))
            self.assertEqual(im.getpixel((128,64)),(255,255,255,55))
            self.assertEqual(im.getpixel((0,0))[3],0)
            self.assertEqual(im.getpixel((255,127))[3],0)
        with Image.open(PLUGIN/'SourceAssets/derived/ui/placement_diamond_outline.png') as im:
            self.assertEqual(im.getpixel((128,64))[3],0)
            for apex in [(128,4),(251,64),(128,123),(4,64)]:
                self.assertEqual(im.getpixel(apex)[3],255,apex)
            self.assertEqual(im.getpixel((0,0))[3],0)
            self.assertGreater(ImageStat.Stat(im.getchannel('A')).mean[0],10)
    def test_source_animation_rows_are_not_facing_columns(self):
        sprites=self.catalog['sprites']
        self.assertEqual(self.catalog['combatTiming']['simulationHz'],72)
        self.assertEqual(self.catalog['combatTiming']['displayHz'],40)
        for sid in ('C1','C2','C3','C4','C5','C6','C7','C8','C10','C11','C12'):
            self.assertEqual(sprites[sid]['animations']['attack']['rows'],[0],sid)
            self.assertEqual(sprites[sid]['animations']['walk']['fps'],0,sid)
            self.assertEqual(sprites[sid]['animations']['death']['availability'],'no_source_sequence',sid)
        self.assertEqual(sprites['C9']['animations']['invisible']['rows'],[1])
        self.assertEqual(sprites['C9']['animations']['attack']['rows'],[0])
        self.assertEqual(sprites['C19']['animations']['idle']['rows'],[1])
        self.assertEqual(sprites['C19']['animations']['walk']['rows'],[1,2,3,4,5])
        self.assertEqual(sprites['C19']['animations']['walk']['fps'],9)
        self.assertEqual(sprites['C14']['animations']['attack']['rows'],[0,1,2])
        self.assertEqual(sprites['C14']['animations']['attack']['fps'],24)
        self.assertEqual(sprites['G1_1']['animations']['attack']['rows'],list(range(8,15)))
        self.assertEqual(sprites['G1_4']['animations']['attack']['rows'],list(range(8,16)))
        # Source selector asks for row18, but the original korath_3 atlas is
        # 2048x1836: only18 rows of102pixels. Never sample outside its image.
        self.assertEqual(sprites['G4_3']['animations']['attack']['rows'],list(range(9,18)))
        self.assertEqual(sprites['G4_3']['animations']['attack']['unavailableSourceRows'],[18])
        self.assertEqual(sprites['G4_3']['animations']['stomp']['availability'],'unavailable_source_frames')
        self.assertEqual(sprites['G5_1']['animations']['idle']['rows'],list(range(10)))
        self.assertEqual(sprites['C1']['sourceFacingOffsetDegrees'],0)
        self.assertEqual(sprites['G1_1']['sourceFacingOffsetDegrees'],-45)
        self.assertEqual(sprites['C17']['unselectedSourceRows'],[1,2,3,4,5])
        for sid,sprite in sprites.items():
            for state,clip in sprite['animations'].items():
                for row in clip['rows']:self.assertTrue(0<=row<sprite['rows'],(sid,state,row))
                if len(clip['rows'])<2:self.assertFalse(clip['loop'],(sid,state))
                self.assertNotIn('columns',clip) # Rows select time; the facing column remains independent.
        monsters={m['id']:m for m in self.catalog['monsters']}
        self.assertAlmostEqual(monsters['C1']['attackIntervalSeconds'],60/72)
        self.assertAlmostEqual(monsters['C1']['sourceMovePixelsPerSecond'],1.2*18)
        self.assertEqual(monsters['C14']['projectileKey'],'fireball')
        self.assertEqual(monsters['C15']['projectileKey'],'healball')
        self.assertNotIn('projectileKey',monsters['C12']) # DAVE rockets require a separate source powerup.
    def test_defense_source_frames_offsets_and_pixel_composites(self):
        buildings={b['id']:b for b in self.catalog['buildings']}
        self.assertNotIn('animation',buildings[20]['levels'][0])
        sniper=buildings[21]['levels'][0]['animation']
        self.assertEqual((sniper['mode'],sniper['directions'],sniper['sourceDirections']),('directional',30,32))
        laser=buildings[23]['levels'][0]['animation']
        self.assertEqual((laser['mode'],laser['directions']),('directional',54))
        tesla=buildings[25]['levels'][0]['animation']
        self.assertEqual((tesla['fps'],tesla['endFps']),(40,20))
        self.assertEqual(tesla['startFrames'],list(range(1,32)))
        self.assertEqual(tesla['attackFrames'],list(range(32,41)))
        self.assertEqual(tesla['endFrames'],list(range(41,55)))
        checked=set()
        for building in self.catalog['buildings']:
            if building['id'] not in (20,21,23,25):continue
            for level in building['levels']:
                if 'animation' not in level:continue
                for state,descriptor,static in [('',level['animation'],level['composite']),('damaged',level['animation'].get('damaged'),level.get('damagedComposite'))]:
                    if not descriptor:continue
                    for key in ('startFrames','attackFrames','endFrames','idleFrames'):
                        for frame in descriptor.get(key,[]):self.assertTrue(0<=frame<descriptor['frames'])
                    if descriptor['texture'] in checked:continue
                    checked.add(descriptor['texture'])
                    with Image.open(PLUGIN/'SourceAssets'/descriptor['texture']) as atlas:
                        fw,fh=descriptor['frameWidth'],descriptor['frameHeight']
                        self.assertEqual(atlas.size,(fw*descriptor['columns'],fh*descriptor['rows']))
                        self.assertLessEqual(descriptor['frames'],descriptor['columns']*descriptor['rows'])
                        with Image.open(PLUGIN/'SourceAssets'/static['texture']) as first:
                            self.assertEqual(first.size,(fw,fh))
                            self.assertIsNone(ImageChops.difference(atlas.crop((0,0,fw,fh)),first).getbbox())
                        # Last source frame is represented and differs from frame0;
                        # this catches a static first-frame atlas repeated everywhere.
                        n=descriptor['frames']-1;x=(n%descriptor['columns'])*fw;y=(n//descriptor['columns'])*fh
                        self.assertIsNotNone(ImageChops.difference(atlas.crop((0,0,fw,fh)).convert('RGB'),atlas.crop((x,y,x+fw,y+fh)).convert('RGB')).getbbox())
                    for layer in descriptor['layers']:
                        self.assertEqual(hashlib.sha256((PLUGIN/'SourceAssets'/layer['texture']).read_bytes()).hexdigest(),layer['sha256'])
        self.assertEqual(len(checked),6)
        fp=buildings[20]['combatFootprint']
        self.assertEqual((fp['minX'],fp['minY'],fp['maxX'],fp['maxY'],fp['centerX'],fp['centerY']),(0,0,70,70,35,35))
        self.assertEqual(buildings[14]['combatFootprint']['width'],130)
        self.assertEqual(fp['pixelsPerGridCell'],20)
    def test_activity_building_source_frames_offsets_and_pixels(self):
        buildings={b['id']:b for b in self.catalog['buildings']}
        expected={1:'collector',2:'collector',3:'collector',4:'collector',8:'unlocking',13:'hatching',26:'researching'}
        checked=set()
        for bid in (*expected,6):
            building=buildings[bid]
            for level in building['levels']:
                descriptor=level['animation']
                self.assertEqual(descriptor['mode'],'resource_fill' if bid==6 else 'activity')
                if bid!=6:self.assertEqual(descriptor['activityGate'],expected[bid])
                self.assertNotIn('damaged',descriptor) # Never overlay normal motion on original damaged art.
                for layer in descriptor['layers']:
                    self.assertEqual(hashlib.sha256((PLUGIN/'SourceAssets'/layer['texture']).read_bytes()).hexdigest(),layer['sha256'])
                if descriptor['texture'] in checked:continue
                checked.add(descriptor['texture'])
                fw,fh=descriptor['frameWidth'],descriptor['frameHeight']
                with Image.open(PLUGIN/'SourceAssets'/descriptor['texture']) as atlas:
                    self.assertEqual(atlas.size,(descriptor['textureWidth'],descriptor['textureHeight']))
                    first=atlas.crop((0,0,fw,fh))
                    with Image.open(PLUGIN/'SourceAssets'/level['composite']['texture']) as static:
                        self.assertEqual(static.size,first.size)
                        self.assertEqual(first.tobytes(),static.convert('RGBA').tobytes())
                    frames=[]
                    for n in range(descriptor['frames']):
                        x=(n%descriptor['columns'])*fw;y=(n//descriptor['columns'])*fh
                        frames.append(atlas.crop((x,y,x+fw,y+fh)).tobytes())
                    self.assertGreater(len(set(frames)),1,'Original animation must contain changing pixels: '+descriptor['texture'])
        report=json.loads((DATA/'building-animation-provenance.json').read_text())
        self.assertEqual(len(checked),report['uniqueAtlases'])
        self.assertEqual(len(report['entries']),sum(len(buildings[bid]['levels']) for bid in (*expected,6)))
        for bid in (5,11,14,15,17,18,20):
            self.assertTrue(all('animation' not in level for level in buildings[bid]['levels']),bid)
        self.assertEqual(buildings[6]['levels'][0]['animation']['fillFrameScale'],26)
        self.assertEqual(buildings[6]['levels'][0]['animation']['frames'],26)
        self.assertEqual(buildings[6]['levels'][0]['animation']['fps'],0)
        self.assertEqual(buildings[8]['levels'][3]['animation']['unavailableSourceFrames'],[20])
        self.assertEqual(buildings[8]['levels'][1]['animation']['unselectedSourceFrames'],[20])
        for name in ('BUILDING1.as','BUILDING2.as','BUILDING3.as','BUILDING4.as','BUILDING6.as','BUILDING8.as','BUILDING13.as','BUILDING15.as','BUILDING26.as','BFOUNDATION.as'):
            self.assertTrue((DATA/'Original/BuildingAnimation'/name).is_file(),name)
    def test_activity_cadence_preserves_original_conditional_ticks(self):
        buildings={b['id']:b for b in self.catalog['buildings']}
        twig=buildings[1]['levels'][0]['animation']
        hatch=buildings[13]['levels'][0]['animation']
        # Original if(fast tick) ... else if(slow tick) advances at the union,
        # including tick10 for producers and tick7 for Hatcheries.
        self.assertEqual(twig['workingFrames'][:16],[0,0,0,1,1,1,2,2,2,3,4,4,5,5,5,6])
        self.assertEqual(hatch['workingFrames'][:16],[0,0,1,1,2,2,3,4,5,5,6,6,7,7,8,8])
        self.assertEqual(twig['workingSlowFrames'][:21],[0]*10+[1]*10+[2])
        self.assertEqual(hatch['workingSlowFrames'][:15],[0]*7+[1]*7+[2])
        self.assertEqual(len(twig['workingFrames']),510)
        self.assertEqual(len(hatch['workingFrames']),210)
        self.assertEqual(hatch['sourcePlaybackFrames'],30)
        self.assertEqual(hatch['unselectedSourceFrames'],[30])
        self.assertEqual(hatch['pausedFrameBehavior'],'reset')
        self.assertEqual(twig['pausedFrameBehavior'],'hold')
        for bid in (1,2,3,4,8,13,26):
            for level in buildings[bid]['levels']:
                anim=level['animation']
                self.assertEqual(anim['fps'],40)
                self.assertEqual(anim['sourceDisplayHz'],40)
                self.assertTrue(anim['sourceRandomStart'])
                self.assertEqual(anim['sourceRandomStartFrames'],anim['sourceDeclaredFrames']-2)
                self.assertEqual(anim['sourceDisplayPhaseCount'],5 if bid in (1,2,3,4,8) else 1)
                self.assertEqual(anim['sourceFrameCycle'],[min(n,anim['frames']-1) for n in range(anim['sourcePlaybackFrames'])])
                self.assertEqual((anim['fastTickDivisor'],anim['slowTickDivisor']),(2,7) if bid==13 else (3,10))
                for key,fast in [('workingFrames',True),('workingSlowFrames',False)]:
                    sequence=anim[key];sourceframe=0;displayed=0
                    for tick in range(len(sequence)*3):
                        fast_advance=fast and tick%(2 if bid==13 else 3)==0
                        slow_advance=tick%(7 if bid==13 else 10)==0
                        if fast_advance or slow_advance:
                            displayed=min(sourceframe,anim['frames']-1)
                            sourceframe=(sourceframe+1)%anim['sourcePlaybackFrames']
                        self.assertEqual(sequence[tick%len(sequence)],displayed,(bid,level['level'],key,tick))
                        self.assertGreaterEqual(displayed,0);self.assertLess(displayed,anim['frames'])
    def test_source_effect_shapes_and_references_are_importable(self):
        effects=self.catalog['effects'];manifest={m['source']:m for m in self.manifest}
        self.assertEqual((effects['cannonball']['sourceShape'],effects['cannonball']['pivotX'],effects['cannonball']['pivotY']),(1836,4,4))
        self.assertEqual((effects['splat']['sourceShape'],effects['splat']['sourceFrame']),(1818,5))
        self.assertEqual(effects['splat']['frames'],1) # Distinct splat variants are never cycled as motion.
        self.assertEqual(effects['smoke']['animationMode'],'particle_life_selects_frame')
        self.assertEqual(effects['lightning']['rgb'],3197178)
        self.assertEqual(effects['laser']['rgb'],16051677)
        for key,effect in effects.items():
            self.assertTrue(manifest[effect['texture']]['importable'],key)
            with Image.open(PLUGIN/'SourceAssets'/effect['texture']) as image:
                self.assertGreaterEqual(image.width,effect['frameWidth']*effect['columns'],key)
                self.assertGreaterEqual(image.height,effect['frameHeight']*effect['rows'],key)
            if effect.get('sha256'):
                self.assertEqual(hashlib.sha256((PLUGIN/'SourceAssets'/effect['texture']).read_bytes()).hexdigest(),effect['sha256'])
        for key in ('hit1','splat1','snipe1','splash1','lightningfire'):
            path=self.catalog['combatSounds'][key]
            self.assertTrue(manifest[path]['importable'],key)
    def test_native_ui_uses_available_original_art_and_staged_fonts(self):
        hud=(PLUGIN/'Source/BackyardMMO/Private/BYMHUD.cpp').read_text()
        paths=[p for p in re.findall(r'TEXT\("([^"\n]+\.(?:png|jpg))"\)',hud) if '/' in p]
        frame_prefix=re.search(r'const FString Prefix\s*=\s*TEXT\("([^"\n]+)"\)',hud)
        self.assertIsNotNone(frame_prefix)
        paths.extend(frame_prefix.group(1)+p for p in re.findall(r'Edge\(TEXT\("([^"\n]+)"\)',hud))
        self.assertGreater(len(paths),10)
        for relative in paths:self.assertTrue((PLUGIN/'SourceAssets'/relative).is_file(),relative)
        provenance=json.loads((DATA/'ui-art-provenance.json').read_text())
        for relative,info in provenance['inputs'].items():
            self.assertEqual(hashlib.sha256((PLUGIN/'SourceAssets'/relative).read_bytes()).hexdigest(),info['sha256'])
        for bar in provenance['resourceBars'].values():
            with Image.open(PLUGIN/'SourceAssets'/bar['texture']) as im:
                self.assertEqual(im.size,(146,44));self.assertEqual(im.mode,'RGBA')
                self.assertEqual(im.getchannel('A').getextrema(),(0,255))
        for source,target in [('31_Groboldov.ttf','GROBOLD.ttf'),('4_Verdana.ttf','Verdana.ttf'),('2960_Verdana Bold.ttf','VerdanaBold.ttf')]:
            self.assertEqual((PLUGIN/'SourceAssets/source_client/fonts'/source).read_bytes(),(PLUGIN/'Content/UI/Fonts'/target).read_bytes())
if __name__=='__main__':unittest.main(verbosity=2)
