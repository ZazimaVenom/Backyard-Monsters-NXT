"""Original worker/mushroom/decoration art regression checks (no Unreal needed)."""
import hashlib,json,unittest
from pathlib import Path
from PIL import Image,ImageChops,ImageOps

PLUGIN=Path(__file__).resolve().parents[1]/'Plugins/BackyardMMO'
ASSETS=PLUGIN/'SourceAssets'
DATA=PLUGIN/'Content/Data'

class WorkerArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog=json.loads((DATA/'catalog.json').read_text())
        cls.report=json.loads((DATA/'worker-art-provenance.json').read_text())

    def test_worker_facing_cells_are_not_animation_frames(self):
        worker=self.catalog['sprites']['worker']
        self.assertEqual((worker['textureWidth'],worker['textureHeight'],worker['frameWidth'],worker['frameHeight']),(810,54,27,27))
        self.assertEqual((worker['directions'],worker['columns'],worker['rows']),(30,30,2))
        self.assertEqual((worker['pivotX'],worker['pivotY']),(9,19))
        for state in ('walk','idle'):
            self.assertEqual(worker['animations'][state]['rows'],[0])
            self.assertEqual(worker['animations'][state]['fps'],0)
        self.assertEqual(worker['animations']['boosted']['rows'],[1])
        self.assertFalse(self.report['worker']['temporalFrameSequence'])

    def test_mushrooms_use_five_exact_source_variants_with_independent_shadows(self):
        variants=self.report['mushrooms']['variants']
        self.assertEqual([v['frame'] for v in variants],[1,2,3,4,5])
        self.assertEqual([v['top']['sourceShape'] for v in variants],[1684,1686,1688,1690,1692])
        for v in variants:
            top=v['top'];shadow=v['shadow']
            self.assertEqual(top['texture'],f"source_client/shapes/{top['sourceShape']}.png")
            original=Image.open(ASSETS/f"source_client/shapes/{shadow['sourceShape']}.png").convert('RGBA')
            actual=Image.open(ASSETS/shadow['texture']).convert('RGBA')
            alpha=ImageChops.multiply(ImageOps.invert(original.convert('L')),original.getchannel('A'))
            self.assertEqual(actual.size,original.size)
            self.assertEqual(actual.getchannel('A').tobytes(),alpha.tobytes())
            self.assertEqual(actual.convert('RGB').getbbox(),None)
        for path,item in self.report['inputs'].items():
            self.assertEqual(hashlib.sha256((ASSETS/path).read_bytes()).hexdigest(),item['sha256'])

    def test_every_decorative_animation_retains_original_frames_and_offsets(self):
        self.assertEqual(len(self.report['decorations']),25)
        self.assertEqual({d['building'] for d in self.report['decorations']},set(range(28,51))|{71,105})
        for item in self.report['decorations']:
            animation=item['animation'];atlas=Image.open(ASSETS/animation['texture']).convert('RGBA')
            self.assertEqual((animation['mode'],animation['fps'],animation['sourceDisplayHz'],animation['sourceTickDivisor']),('decoration',20,40,2))
            width,height=animation['frameWidth'],animation['frameHeight']
            for frame in range(animation['frames']):
                expected=Image.new('RGBA',(width,height),(0,0,0,0))
                for layer in animation['layers']:
                    original=Image.open(ASSETS/layer['texture']).convert('RGBA')
                    self.assertEqual(hashlib.sha256((ASSETS/layer['texture']).read_bytes()).hexdigest(),layer['sha256'])
                    if layer['layer']=='shadow':
                        alpha=ImageOps.invert(original.convert('L'));original=Image.new('RGBA',original.size,(0,0,0,0));original.putalpha(alpha)
                    elif layer['layer'].startswith('anim'):
                        x=(frame%layer['availableFrames'])*layer['frameWidth']
                        original=original.crop((x,0,x+layer['frameWidth'],layer['frameHeight']))
                    expected.alpha_composite(original,(layer['offset'][0]+animation['pivotX'],layer['offset'][1]+animation['pivotY']))
                x=(frame%animation['columns'])*width;y=(frame//animation['columns'])*height
                self.assertEqual(atlas.crop((x,y,x+width,y+height)).tobytes(),expected.tobytes(),(item['building'],frame))

    def test_added_art_is_importable_color_data(self):
        manifest={entry['source']:entry for entry in json.loads((DATA/'asset-manifest.json').read_text())['assets']}
        paths=['source_client/shapes/517.png']+[v['shadow']['texture'] for v in self.report['mushrooms']['variants']]
        paths += [d['animation']['texture'] for d in self.report['decorations']]
        for path in paths:
            self.assertTrue(manifest[path]['importable'],path)
            self.assertEqual(hashlib.sha256((ASSETS/path).read_bytes()).hexdigest(),manifest[path]['sha256'])
            self.assertIn(manifest[path]['mode'],('RGBA','RGB','P'))

if __name__=='__main__':unittest.main()
