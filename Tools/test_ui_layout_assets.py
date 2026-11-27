"""Source-data checks for native UI sizing; these do not execute or render Slate.

The UI sizes its brushes from these canonical PNG dimensions even before Unreal
has finished importing/compiling a texture. Read the real source PNG headers so
stale metadata and placeholder dimensions cannot silently become layout truth.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import unittest


class UILayoutAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = Path(__file__).resolve().parents[1] / 'Plugins/BackyardMMO'
        cls.assets = cls.plugin / 'SourceAssets'
        cls.manifest = json.loads((cls.plugin / 'Content/Data/asset-manifest.json').read_text())
        cls.entries = {item['source']: item for item in cls.manifest['assets']}
        cls.provenance = json.loads((cls.plugin / 'Content/Data/ui-art-provenance.json').read_text())

    def png_size(self, relative: str):
        raw = (self.assets / relative).read_bytes()
        self.assertEqual(raw[:8], b'\x89PNG\r\n\x1a\n', relative)
        self.assertEqual(raw[12:16], b'IHDR', relative)
        return struct.unpack('>II', raw[16:24]), raw

    def check_canonical_png(self, relative: str):
        self.assertIn(relative, self.entries)
        entry = self.entries[relative]
        size, raw = self.png_size(relative)
        self.assertTrue(entry['importable'], relative)
        self.assertEqual((entry['width'], entry['height']), size, relative)
        self.assertGreater(size[0], 0, relative)
        self.assertGreater(size[1], 0, relative)
        self.assertEqual(entry['bytes'], len(raw), relative)
        self.assertEqual(entry['sha256'], hashlib.sha256(raw).hexdigest(), relative)
        self.assertTrue(entry['objectPath'].startswith('/BackyardMMO/Art/'), relative)
        return size

    def test_logo_preserves_original_wide_aspect_before_texture_readiness(self):
        self.assertEqual(self.check_canonical_png('ui/title.png'), (518, 160))
        self.assertGreater(self.entries['ui/title.png']['width'] / self.entries['ui/title.png']['height'], 3.)

    def test_all_ui_png_dimensions_match_original_source_and_are_importable(self):
        ui = [relative for relative in self.entries if relative.endswith('.png')
              and (relative.startswith('ui/') or relative.startswith('derived/ui/')
                   or (relative.startswith('source_client/images/') and '_frame1_' in relative))]
        self.assertGreater(len(ui), 20)
        for relative in ui:
            with self.subTest(asset=relative):
                self.check_canonical_png(relative)

    def test_canonical_resource_bars_agree_with_retained_ui_provenance(self):
        for resource, metadata in self.provenance['resourceBars'].items():
            with self.subTest(resource=resource):
                self.assertEqual(self.check_canonical_png(metadata['texture']), (metadata['width'], metadata['height']))
                self.assertEqual((metadata['width'], metadata['height']), (146, 44))
        for relative, metadata in self.provenance['inputs'].items():
            with self.subTest(original_input=relative):
                self.assertEqual(self.check_canonical_png(relative), (metadata['width'], metadata['height']))
                self.assertEqual(self.entries[relative]['sha256'], metadata['sha256'])

    def test_stretchable_skin_sizes_match_original_ui(self):
        self.assertEqual(self.check_canonical_png('ui/wood1.png'), (276, 54))
        self.assertEqual(self.check_canonical_png('ui/stone3.png'), (79, 36))
        self.assertEqual(self.check_canonical_png('alliances/redx.png'), (27, 27))


if __name__ == '__main__':
    unittest.main(verbosity=2)
