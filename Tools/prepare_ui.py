"""Prepare native HUD textures from preserved original Flash artwork.

Only source pixels are composited. ResourceBar1..4 placement comes from symbols
563, 566, 569 and 573 in the original assets.swf, including the goo clipping
rectangle. Native Slate supplies live values and interactions; no Flash runtime
or external UI renderer is required by the game or this preparation step.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


RESOURCE_LAYOUT = (
    ('twigs', 563, 'source_client/shapes/558.png', (-9, -4), None),
    ('pebbles', 566, 'source_client/shapes/565.png', (-6, -2), None),
    ('putty', 569, 'source_client/shapes/568.png', (-5, -1), None),
    ('goo', 573, 'source_client/shapes/572.png', (-4, -5), (0, 0, 104, 41)),
)
RESOURCE_BACKGROUND = 'source_client/shapes/552.png'
RESOURCE_CANVAS = (146, 44)
RESOURCE_ORIGIN = (10, 5)


def prepare_ui(assets: Path, data: Path | None = None) -> dict:
    """Write source-composed resource bars and return reproducible provenance."""
    inputs = {}

    def original(relative: str) -> Image.Image:
        path = assets / relative
        raw = path.read_bytes()
        with Image.open(path) as image:
            result = image.convert('RGBA')
        inputs[relative] = {
            'sha256': hashlib.sha256(raw).hexdigest(),
            'width': result.width,
            'height': result.height,
        }
        return result

    background = original(RESOURCE_BACKGROUND)
    if background.size != (136, 34):
        raise ValueError('Original ResourceBar background shape 552 must be 136 x 34')
    outputs = {}
    for name, symbol, relative, bounds_origin, crop in RESOURCE_LAYOUT:
        icon = original(relative)
        if crop:
            icon = icon.crop(crop)
        result = Image.new('RGBA', RESOURCE_CANVAS, (0, 0, 0, 0))
        result.alpha_composite(background, RESOURCE_ORIGIN)
        position = (RESOURCE_ORIGIN[0] + bounds_origin[0],
                    RESOURCE_ORIGIN[1] + bounds_origin[1])
        result.alpha_composite(icon, position)
        output = f'derived/ui/original/resource_{name}.png'
        target = assets / output
        target.parent.mkdir(parents=True, exist_ok=True)
        result.save(target)
        outputs[name] = {
            'texture': output,
            'symbol': symbol,
            'background': RESOURCE_BACKGROUND,
            'icon': relative,
            'iconOrigin': list(bounds_origin),
            'iconCrop': list(crop) if crop else None,
            'width': RESOURCE_CANVAS[0],
            'height': RESOURCE_CANVAS[1],
            'registrationOffset': list(RESOURCE_ORIGIN),
            'valuePosition': [39, 15],
            'sourceFontId': 31,
            'sourceFontPixels': 12,
            'sourceTextRGBA': [255, 255, 255, 255],
        }
    provenance = {
        'schema': 1,
        'source': 'client/scripts/_assets/assets.swf:ResourceBar1..4',
        'behaviorSources': ['client/scripts/UI_TOP.as', 'client/scripts/UI_MENU.as',
                            'client/scripts/StoneButton.as', 'client/scripts/BUILDINGSPOPUP.as'],
        'method': 'Original exported shapes composited at original local coordinates; live text and fill omitted for native UI.',
        'inputs': inputs,
        'resourceBars': outputs,
        'originalMenu': {
            'background': 'ui/wood1.png',
            'buttonVariants': ['ui/stone1.png', 'ui/stone2.png', 'ui/stone3.png'],
            'anchor': 'bottom right',
            'screenMargin': 10,
            'woodMargin': 10,
            'buttonSpacing': 3,
            'buttonHeight': 36,
            'sourceLabels': ['Buildings', 'Quests', 'Store', 'Map'],
        },
        'originalConstruction': {
            'categories': ['Resources', 'Buildings', 'Defensive', 'Decorations'],
            'columns': 5, 'rows': 2,
            'columnPitch': 130, 'rowPitch': 170,
            'cardPixels': [120, 160],
        },
    }
    if data is not None:
        data.mkdir(parents=True, exist_ok=True)
        (data / 'ui-art-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return provenance


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--plugin', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'Plugins/BackyardMMO')
    options = parser.parse_args()
    result = prepare_ui(options.plugin / 'SourceAssets', options.plugin / 'Content/Data')
    print(f"Prepared {len(result['resourceBars'])} original resource-bar textures")
