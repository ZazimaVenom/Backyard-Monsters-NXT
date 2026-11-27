"""Original Map Room 3 geometry/art preservation and complete atlas pixel checks."""
import hashlib
import json
import math
from pathlib import Path
import unittest
from PIL import Image, ImageChops, ImageOps
from prepare_maproom_art import build_maproom_art, flash_hash, select_tile

PLUGIN=Path(__file__).resolve().parents[1]/'Plugins/BackyardMMO'
ASSETS=PLUGIN/'SourceAssets'
DATA=PLUGIN/'Content/Data'


class MapRoomArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.art=json.loads((DATA/'maproom-art.json').read_text())
        cls.buildings=json.loads((DATA/'maproom-building-art.json').read_text())
        cls.manifest={item['source']:item for item in json.loads((DATA/'asset-manifest.json').read_text())['assets']}

    def test_metadata_is_reproducible_from_exact_retained_source(self):
        self.assertEqual(build_maproom_art(DATA/'Original/MapRoom3Art',ASSETS),self.art)
        for reference in self.art['sourceReferences']+self.buildings['sourceReferences']:
            raw=(DATA/'Original/MapRoom3Art'/reference['path']).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(),reference['sha256'])

    def test_original_images_and_manifest_dimensions_remain_exact(self):
        self.assertEqual(len(self.art['inputs']),73)
        for path,entry in self.art['inputs'].items():
            raw=(ASSETS/path).read_bytes();manifest=self.manifest[path]
            self.assertEqual(hashlib.sha256(raw).hexdigest(),entry['sha256'])
            self.assertEqual(manifest['sha256'],entry['sha256'])
            self.assertTrue(manifest['importable'])
            with Image.open(ASSETS/path) as image:
                self.assertEqual(image.size,(entry['width'],entry['height']))
                self.assertEqual(image.size,(manifest['width'],manifest['height']))

    def test_hex_geometry_and_original_anchor_policy(self):
        geometry=self.art['geometry']
        self.assertEqual((geometry['hexWidth'],geometry['hexHeight'],geometry['rowStride'],geometry['oddRowOffset']),(104,68,50,52))
        self.assertEqual(geometry['hexVertices'],[[52,0],[104,17],[104,50],[52,68],[0,50],[0,17]])
        for item in self.art['tiles']:
            self.assertEqual(item['offsetX'],(104-item['imageWidth'])/2)
            self.assertEqual(item['offsetY'],68-item['imageHeight'] if item['imageHeight']>68 else (68-item['imageHeight'])/2)
        for item in self.art['icons'].values():
            self.assertEqual(item['offsetX'],math.floor((104-item['imageWidth'])/2+.5))
            self.assertEqual(item['offsetY'],math.floor((68-item['imageHeight'])/2+.5))
        self.assertEqual(self.art['icons']['stronghold']['offsetY'],-30)
        self.assertEqual(self.art['icons']['playerBase']['offsetY'],-16)

    def test_source_altitude_boundaries_and_empty_ranges(self):
        self.assertEqual(len(self.art['tiles']),28)
        self.assertEqual((self.art['blockedAltitude'],self.art['borderAltitude']),(51,100))
        expected={32:'clover01',35:'clover01',36:'clover02',38:'clover02',49:'clover06',50:'clover06',
                  51:'brownplant01',52:'brownplant01',53:'brownplant02',60:'brownplant05',
                  61:'greenplant01',70:'greenplant05',71:'spiky01',72:'spiky02',80:'spiky07'}
        for altitude,name in expected.items():
            self.assertEqual(Path(select_tile(self.art,altitude,0)['path']).stem,name)
        for altitude in (-10,0,31,81,98,99,101,127):
            self.assertIsNone(select_tile(self.art,altitude,0))
        for cell in range(1000):
            self.assertTrue(Path(select_tile(self.art,100,cell)['path']).stem.startswith('borderplant'))

    def test_source_signed_hash_shift_count_and_overlap_variation(self):
        # Independent JavaScript signed-bitwise reference vectors, whose shift
        # conversion matches AS3. >>>35 wraps to3, not a 64-bit shift by35.
        vectors=((0,0),(1,40108049),(2,80216098),(499,1553195747),(500,1404035734),
                 (12345,2072466910),(249999,774177662),(0x7fffffff,1847853056),
                 (0x80000000,1879048192),(0xffffffff,31195136))
        for index,expected in vectors:self.assertEqual(flash_hash(index),expected)
        variants={Path(select_tile(self.art,79,index)['path']).stem for index in range(500)}
        self.assertEqual(variants,{'spiky06','spiky07'})
        borders={Path(select_tile(self.art,100,index)['path']).stem for index in range(500)}
        self.assertEqual(borders,{f'borderplant{i:02d}' for i in range(1,6)})

    def test_overlays_and_effect_cells_keep_source_extents(self):
        self.assertEqual(self.art['background']['path'],'worldmap/background.jpg')
        self.assertEqual((self.art['background']['imageWidth'],self.art['background']['imageHeight']),(1200,1200))
        for item in self.art['overlays'].values():self.assertEqual((item['imageWidth'],item['imageHeight']),(104,68))
        for item in self.art['strongholdEffects'].values():
            self.assertEqual((item['frames'],item['frameWidth'],item['frameHeight']),(40,100,110))
            self.assertEqual((item['offsetX'],item['offsetY']),(2,-41))
            self.assertEqual(item['frameWidth']*item['frames'],item['imageWidth'])
        self.assertEqual((self.art['damageBar']['frames'],self.art['damageBar']['frameWidth'],self.art['damageBar']['frameHeight']),(17,41,4))

    def test_all_original_relationship_frame_pixels_and_alpha(self):
        checked=set()
        for item in self.buildings['entries']:
            for relationship,descriptor in item['relationships'].items():
                path=descriptor['texture']
                if path in checked:continue
                checked.add(path)
                atlas=Image.open(ASSETS/path).convert('RGBA')
                width,height=descriptor['frameWidth'],descriptor['frameHeight']
                self.assertEqual(atlas.size,(descriptor['textureWidth'],descriptor['textureHeight']))
                self.assertEqual(hashlib.sha256((ASSETS/path).read_bytes()).hexdigest(),self.manifest[path]['sha256'])
                frame_hashes=set()
                for frame in range(32):
                    expected=Image.new('RGBA',(width,height),(0,0,0,0))
                    for layer in descriptor['layers']:
                        image=Image.open(ASSETS/layer['texture']).convert('RGBA')
                        self.assertEqual(hashlib.sha256((ASSETS/layer['texture']).read_bytes()).hexdigest(),layer['sha256'])
                        if layer['layer']=='anim':
                            self.assertEqual(Path(layer['texture']).name,relationship+'.light.png')
                            start=frame*layer['frameWidth'];image=image.crop((start,0,start+layer['frameWidth'],layer['frameHeight']))
                        elif layer['layer']=='shadow':
                            alpha=ImageChops.multiply(ImageOps.invert(image.convert('L')),image.getchannel('A'))
                            image=Image.new('RGBA',image.size,(0,0,0,0));image.putalpha(alpha)
                        expected.alpha_composite(image,(layer['offset'][0]+descriptor['pivotX'],layer['offset'][1]+descriptor['pivotY']))
                    x=(frame%descriptor['columns'])*width;y=(frame//descriptor['columns'])*height
                    frame_pixels=atlas.crop((x,y,x+width,y+height)).tobytes()
                    self.assertEqual(frame_pixels,expected.tobytes(),(relationship,frame))
                    frame_hashes.add(hashlib.sha256(frame_pixels).hexdigest())
                self.assertEqual(len(frame_hashes),32)
        self.assertEqual(len(checked),4)

    def test_all_five_defender_levels_reuse_exact_source_light_cycle(self):
        self.assertEqual(len(self.buildings['entries']),5)
        catalog=json.loads((DATA/'catalog.json').read_text())
        building=next(item for item in catalog['buildings'] if item['id']==140)
        self.assertEqual([level['hp'] for level in building['levels']],[8800,42000,200000,400000,600000])
        for level,item in zip(building['levels'],self.buildings['entries']):
            self.assertEqual(level['mapRoomRelationshipAnimations'],item['relationships'])
            for descriptor in item['relationships'].values():
                self.assertEqual((descriptor['frames'],descriptor['sourceDisplayHz'],descriptor['fastTickDivisor'],descriptor['slowTickDivisor']),(32,40,1,1))
                self.assertEqual(descriptor['sourceFrameCycle'],list(range(32)))
                self.assertFalse(descriptor['sourceRandomStart'])
                self.assertNotIn('damaged',descriptor)

    def test_stronghold_preserves_source_tesla_effect_and_event_origin(self):
        catalog=json.loads((DATA/'catalog.json').read_text())
        tower=next(item for item in catalog['buildings'] if item['id']==138)
        presentation=tower['combatPresentation']
        self.assertEqual(presentation['projectile'],'lightning')
        self.assertEqual(presentation['sound'],'lightningfire')
        self.assertEqual(presentation['muzzleOffsetPixels'],[0,0])
        for level in tower['levels']:
            self.assertEqual(level['attackIntervalSeconds'],1)
            self.assertEqual(level['attackIntervalSeconds'],level['attackRateTicks'])
            self.assertFalse(any(key.startswith('anim') for key in level['imageLayers']))
        source=(DATA/'Original/MapRoom3Art/client/scripts/GuardTower.as').read_text()
        self.assertIn('SOUNDS.Play("lightningfire", 0.8)',source)
        self.assertIn('EFFECTS.Lightning(this.m_x, this.m_y,',source)


if __name__=='__main__':unittest.main()
