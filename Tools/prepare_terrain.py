"""Compose the original grass terrain using MAPBG.MakeTile's layer recipe.

The seven embedded images are rectangular texture samples, not diamond sprites.
MAPBG.as repeats each independently into a 1000 x 500 layer, then blends layers
2..7 through stitched, two-octave turbulence masks. This module preserves that
recipe, image order, native pixel scale, source alpha and seed offsets. It does
not resize, recolor, blur or generate substitute artwork.

Noise is a Python adaptation of the SVG 1.1 feTurbulence reference algorithm,
with Flash's grayscale-alpha channel selection and byte rounding. The original
Flash player has not been used for a pixel-exact comparison. Pillow's integer
alpha compositing can differ from Flash by a small rounding amount.

References:
  client/scripts/MAPBG.as:MakeTile (included in Content/Data/Original)
  https://www.w3.org/TR/SVG11/filters.html#feTurbulenceElement
  https://github.com/ruffle-rs/ruffle/blob/master/core/src/bitmap/operations.rs

W3C NOTICE (noise algorithm only)
Copyright (c) 2015 W3C (MIT, ERCIM, Keio, Beihang).
This software includes material derived from the SVG 1.1 feTurbulence reference
implementation at https://www.w3.org/TR/SVG11/filters.html#feTurbulenceElement.
Changes: Python adaptation, periodic sampling, alpha-mask rasterization and
Flash grayscale-alpha channel selection/rounding for this terrain compositor.
https://www.w3.org/copyright/software-license-2015/

By obtaining and/or copying this work, you (the licensee) agree that you have
read, understood, and will comply with the following terms and conditions.

Permission to copy, modify, and distribute this work, with or without
modification, for any purpose and without fee or royalty is hereby granted,
provided that you include the following on ALL copies of the work or portions
thereof, including modifications:
1. The full text of this NOTICE in a location viewable to users of the
   redistributed or derivative work.
2. Any pre-existing intellectual property disclaimers, notices, or terms and
   conditions. If none exist, the W3C Software and Document Short Notice
   should be included.
3. Notice of any changes or modifications, through a copyright statement on
   the new code or document such as "This software or document includes
   material copied from or derived from [title and URI of the W3C document].
   Copyright (c) [YEAR] W3C (MIT, ERCIM, Keio, Beihang)."

THIS WORK IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS
OR WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO, WARRANTIES
OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF
THE SOFTWARE OR DOCUMENT WILL NOT INFRINGE ANY THIRD PARTY PATENTS,
COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS.

COPYRIGHT HOLDERS WILL NOT BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL OR
CONSEQUENTIAL DAMAGES ARISING OUT OF ANY USE OF THE SOFTWARE OR DOCUMENT.

The name and trademarks of copyright holders may NOT be used in advertising
or publicity pertaining to the work without specific, written prior
permission. Title to copyright in this work will at all times remain with
copyright holders.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageChops


# Exact BitmapData embeds used by isograss1.as through isograss7.as. The server's
# PNG re-exports have slightly different RGB values; use the embedded originals.
GRASS_SOURCES = (
    'source_client/images/2174_isograss1_isograss1.jpg',
    'source_client/images/2175_isograss2_isograss2.png',
    'source_client/images/2172_isograss3_isograss3.png',
    'source_client/images/2173_isograss4_isograss4.png',
    'source_client/images/2177_isograss5_isograss5.png',
    'source_client/images/2178_isograss6_isograss6.png',
    'source_client/images/2176_isograss7_isograss7.png',
)
TILE_SIZE = (1000, 500)
SWATCH_SIZE = (200, 100)
DEFAULT_SEED = 0  # Original server's save.model.ts default baseseed.
_RANDOM_MODULUS = 2147483647


def _next_random(seed: int) -> int:
    """Park-Miller sequence specified by the SVG reference implementation."""
    return (16807 * seed) % _RANDOM_MODULUS


def _stitched_frequency(size: int, base: float) -> float:
    frequency = 1.0 / base
    low = math.floor(size * frequency) / size
    high = math.ceil(size * frequency) / size
    # Compare relative changes, as in the reference; avoid division by zero
    # when fewer than one noise lattice period would fit into the texture.
    return low if low > 0 and frequency / low < high / frequency else high


class TerrainNoise:
    """Periodic two-octave turbulence for Flash grayscale ALPHA masks.

    sample() returns the turbulence sum before the Flash byte conversion.
    Grayscale RGB uses gradient channel 0, while grayscale ALPHA uses channel 1.
    """

    def __init__(self, seed: int, base_x: float, base_y: float,
                 size: tuple[int, int] = TILE_SIZE):
        if min(size) <= 0 or base_x <= 0 or base_y <= 0:
            raise ValueError('Noise dimensions and bases must be positive')
        self.size = size
        self.frequency = (_stitched_frequency(size[0], base_x),
                          _stitched_frequency(size[1], base_y))
        self.periods = (int(size[0] * self.frequency[0] + .5),
                        int(size[1] * self.frequency[1] + .5))
        seed = int(seed)
        if seed <= 0:
            seed = abs(seed) % (_RANDOM_MODULUS - 1) + 1
        seed = min(seed, _RANDOM_MODULUS - 1)
        gradients = []
        for channel in range(4):
            table = []
            for index in range(256):
                seed = _next_random(seed)
                x = (seed % 512 - 256) / 256.0
                seed = _next_random(seed)
                y = (seed % 512 - 256) / 256.0
                length = math.sqrt(x * x + y * y)
                table.append((x / length, y / length) if length else (0.0, 0.0))
            gradients.append(table)
        permutation = list(range(256))
        for index in range(255, 0, -1):
            seed = _next_random(seed)
            other = seed % 256
            permutation[index], permutation[other] = permutation[other], permutation[index]
        self.permutation = permutation * 2
        self.gradient = gradients[1]  # Alpha in Flash's grayscale branch.

    @staticmethod
    def _coordinate(value: float, frequency: float, period: int):
        # The reference adds PerlinN=4096 before integer conversion. Retaining
        # that step also retains its floating point behavior for native pixels.
        position = value * frequency + 4096.0
        integer = int(position)
        delta = position - integer
        first, second = integer, integer + 1
        wrap = 4096 + period
        if first >= wrap:
            first -= period
        if second >= wrap:
            second -= period
        return first & 255, second & 255, delta, delta - 1.0, delta * delta * (3.0 - 2.0 * delta)

    def _noise(self, x, y) -> float:
        x0, x1, dx0, dx1, sx = x
        y0, y1, dy0, dy1, sy = y
        table, gradient = self.permutation, self.gradient
        a, b = table[x0], table[x1]
        q = gradient[table[a + y0]]
        u = dx0 * q[0] + dy0 * q[1]
        q = gradient[table[b + y0]]
        v = dx1 * q[0] + dy0 * q[1]
        top = u + sx * (v - u)
        q = gradient[table[a + y1]]
        u = dx0 * q[0] + dy1 * q[1]
        q = gradient[table[b + y1]]
        v = dx1 * q[0] + dy1 * q[1]
        bottom = u + sx * (v - u)
        return top + sy * (bottom - top)

    def sample(self, x: float, y: float) -> float:
        x, y = x % self.size[0], y % self.size[1]
        total = 0.0
        for octave in range(2):
            ratio = 1 << octave
            cx = self._coordinate(x, self.frequency[0] * ratio, self.periods[0] * ratio)
            cy = self._coordinate(y, self.frequency[1] * ratio, self.periods[1] * ratio)
            total += abs(self._noise(cx, cy)) / ratio
        return total

    def alpha_mask(self) -> Image.Image:
        width, height = self.size
        # Precompute the repeated axis calculations; no NumPy or Flash runtime
        # is required to regenerate the prepared plugin artwork.
        axes = []
        for octave in range(2):
            ratio = 1 << octave
            xs = [self._coordinate(x, self.frequency[0] * ratio, self.periods[0] * ratio)
                  for x in range(width)]
            ys = [self._coordinate(y, self.frequency[1] * ratio, self.periods[1] * ratio)
                  for y in range(height)]
            axes.append((xs, ys))
        pixels = bytearray(width * height)
        noise = self._noise
        first_x, first_y = axes[0]
        second_x, second_y = axes[1]
        for y in range(height):
            y0, y1 = first_y[y], second_y[y]
            row = y * width
            for x in range(width):
                value = abs(noise(first_x[x], y0)) + abs(noise(second_x[x], y1)) * .5
                pixels[row + x] = min(255, int(value * 255.0 + .5))
        return Image.frombytes('L', self.size, bytes(pixels))


def grass_mask(layer: int, seed: int = DEFAULT_SEED) -> Image.Image:
    if not 2 <= layer <= len(GRASS_SOURCES):
        raise ValueError('Grass overlay layer must be between 2 and 7')
    return TerrainNoise(seed + 1 + layer, 50 * layer, 25 * layer).alpha_mask()


def build_grass_tile(assets: Path, seed: int = DEFAULT_SEED) -> Image.Image:
    """Return an opaque 1000 x 500 composition of the seven original embeds."""
    result = None
    for index, relative in enumerate(GRASS_SOURCES, 1):
        with Image.open(assets / relative) as source:
            if source.size != SWATCH_SIZE:
                raise ValueError(f'{relative}: expected original 200 x 100 texture sample')
            swatch = source.convert('RGBA')
        layer = Image.new('RGBA', TILE_SIZE)
        for y in range(0, TILE_SIZE[1], SWATCH_SIZE[1]):
            for x in range(0, TILE_SIZE[0], SWATCH_SIZE[0]):
                layer.paste(swatch, (x, y))
        if result is None:
            result = layer
        else:
            # copyPixels(..., alphaBitmapData, ..., mergeAlpha=true) multiplies
            # the source's alpha by the separate mask; source alpha is not lost.
            layer.putalpha(ImageChops.multiply(layer.getchannel('A'), grass_mask(index, seed)))
            result.alpha_composite(layer)
    assert result is not None
    if result.getchannel('A').getextrema() != (255, 255):
        raise ValueError('The original opaque base must cover the complete terrain tile')
    return result


def terrain_metadata(seed: int = DEFAULT_SEED) -> dict:
    return {
        'source': 'client/scripts/MAPBG.as:MakeTile',
        'seed': seed,
        'pixelWidth': TILE_SIZE[0],
        'pixelHeight': TILE_SIZE[1],
        'sourceSamples': list(GRASS_SOURCES),
        'sampleWidth': SWATCH_SIZE[0],
        'sampleHeight': SWATCH_SIZE[1],
        'octaves': 2,
        'stitch': True,
        'fractalNoise': False,
        'alphaGradientChannelIndex': 1,
        'baseXPerLayer': 50,
        'baseYPerLayer': 25,
        'maskSeed': 'seed + 1 + layer (layers 2..7)',
        'pixelMatchStatus': 'Source recipe port; not pixel-verified against Flash Player',
    }
