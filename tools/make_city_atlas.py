#!/usr/bin/env python3
"""Re-grade the KayKit citybits atlas into a coherent city palette.

The stock atlas is a 8x4 grid of vertical gradient swatches, and several of them
are fully saturated primaries - a pure red, a pure green, a pure teal, a gold.
Each of building_A..H picks a different one for its facade, which is why a street
of them reads as a row of coloured blocks rather than as a city.

This rewrites each swatch's hue and saturation while leaving its *lightness
gradient* alone - that gradient is the baked shading and window detail, and it is
the only thing making the flat kit geometry read as architecture. What comes out
is the same atlas in brick, sandstone, concrete and weathered blue-grey, so the
buildings still differ from one another but differ the way real ones do.

Accent swatches that are not architecture (the fire hydrant's red, the traffic
light's amber) are deliberately left saturated.

    python tools/make_city_atlas.py

reads  assets/city/kaykit/citybits_texture.png
writes assets/city/kaykit/citybits_city.png
"""

import colorsys
import os
import sys

from PIL import Image

SRC = "assets/city/kaykit/citybits_texture.png"
DST = "assets/city/kaykit/citybits_city.png"

COLS = 8       # swatch columns
SWATCH_ROWS = 4  # each swatch is two atlas cells tall

# (hue_deg | None to keep, saturation scale, lightness scale, lightness offset)
# indexed [swatch_row][col]; swatch_row 0 is the top pair of cells.
KEEP = (None, 1.0, 1.0, 0.0)

GRADE = [
    # --- swatch row 0: darks, whites, greys, browns -----------------------
    [
        (210, 0.50, 1.00, 0.00),   # c0 near-black    -> cool charcoal (glass, trim)
        ( 35, 0.45, 0.99, 0.00),   # c1 white         -> warm off-white stone
        ( 32, 0.50, 0.95, 0.00),   # c2 grey          -> warm concrete (roads + main neutral)
        (210, 0.60, 1.00, 0.00),   # c3 dark slate
        KEEP,                      # c4 neutral dark
        ( 17, 0.72, 0.96, 0.00),   # c5 terracotta    -> brick
        ( 20, 0.80, 1.00, 0.00),   # c6 brown
        ( 16, 0.50, 1.00, 0.00),   # c7 orange-brown  -> muted brick
    ],
    # --- swatch row 1: the saturated blues, gold, red ----------------------
    [
        (208, 0.28, 0.97, 0.00),   # c0 light blue    -> pale grey-blue
        (212, 0.28, 1.02, 0.02),   # c1 strong blue   -> slate
        (208, 0.45, 1.00, 0.00),   # c2 steel blue    -> building_H facade
        ( 36, 0.42, 1.00, 0.00),   # c3 gold          -> sandstone (building_E/F)
        (  4, 0.85, 1.00, 0.00),   # c4 red           -> the fire hydrant; stays red
        ( 32, 0.75, 1.00, 0.00),   # c5 tan/cream     -> already good beige
        KEEP,                      # c6 warm grey
        KEEP,                      # c7 brown-grey
    ],
    # --- swatch row 2: yellow-green, teals, green, salmon ------------------
    [
        ( 45, 0.26, 1.00, 0.00),   # c0 yellow-green  -> khaki stone
        (200, 0.20, 1.25, 0.03),   # c1 teal          -> grey-blue
        ( 30, 0.25, 1.45, 0.02),   # c2 green         -> building_D facade, warm stone
        (205, 0.30, 1.40, 0.02),   # c3 dark teal     -> building_A/G facade, blue slate
        KEEP,                      # c4 grey gradient
        ( 33, 0.90, 1.00, 0.00),   # c5 cream
        ( 12, 0.45, 1.00, 0.00),   # c6 orange-red    -> muted brick red
        ( 10, 0.45, 0.97, 0.00),   # c7 salmon        -> building_C/F facade, clay brick
    ],
    # --- swatch row 3: olive greys, an amber accent, plain greys -----------
    [
        KEEP,                      # c0 olive grey
        KEEP,                      # c1 olive grey
        ( 38, 0.80, 1.00, 0.00),   # c2 amber accent (signage) - stays warm and bright
        KEEP, KEEP, KEEP, KEEP, KEEP,
    ],
]


def grade_pixel(rgba, hue, sat_scale, light_scale, light_off):
    r, g, b, a = rgba
    h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    if hue is not None:
        h = hue / 360.0
    s = max(0.0, min(1.0, s * sat_scale))
    l = max(0.0, min(1.0, l * light_scale + light_off))
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return (int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5), a)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(root, SRC)
    dst = os.path.join(root, DST)
    if not os.path.exists(src):
        sys.exit("missing %s - run from the project root" % SRC)

    im = Image.open(src).convert("RGBA")
    w, h = im.size
    cw = w // COLS
    ch = h // SWATCH_ROWS
    px = im.load()

    # one lookup per distinct source colour per swatch: the swatches are smooth
    # gradients over a few hundred unique values, so this turns a 1M-pixel
    # conversion into a few thousand.
    for sr in range(SWATCH_ROWS):
        for col in range(COLS):
            hue, sat, lsc, loff = GRADE[sr][col]
            if (hue, sat, lsc, loff) == KEEP:
                continue
            cache = {}
            for y in range(sr * ch, (sr + 1) * ch):
                for x in range(col * cw, (col + 1) * cw):
                    p = px[x, y]
                    out = cache.get(p)
                    if out is None:
                        out = grade_pixel(p, hue, sat, lsc, loff)
                        cache[p] = out
                    px[x, y] = out

    im.save(dst)
    print("wrote %s (%dx%d)" % (DST, w, h))


if __name__ == "__main__":
    main()
