"""Generates font_aa.c: smooth (anti-aliased) fonts, 4 bits per pixel, from DejaVu Sans (proportional) and
JetBrains Mono (monospace, the "Smooth mono" choice).

DejaVu Sans is under the Bitstream Vera license, JetBrains Mono under the SIL Open Font License 1.1 - both
free to embed and sell as part of a product; the notices below are copied into the generated file.
DejaVu comes with matplotlib, JetBrains Mono is in ttf/ next to this file (from Google Fonts, with OFL.txt).
Run from this folder:

    uvx --from matplotlib python fontgen_aa.py [preview.png]

Every font holds ASCII 32..95 (uppercase only - the firmware prints lowercase as uppercase) plus the
degree sign as glyph 64 ("\\177" in C strings, GFX_DEG in gfx.h). The y passed to gfx_text is the top of
the capital letters; rows above it (round overshoot) and below the baseline (, ; Q J) are stored too.

M and W are not taken from DejaVu: at these sizes its wide M and W came out blurry (stems between pixels,
smeared diagonals). They are drawn here instead (MW below) - straight stems on whole pixels, smooth
diagonals, narrower."""
import os
import sys

import matplotlib
from PIL import Image, ImageDraw, ImageFont

TTF = os.path.join(matplotlib.get_data_path(), "fonts", "ttf")
LOCAL_TTF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ttf")
DEG = "°"
ALL = [chr(c) for c in range(32, 96)] + [DEG]
DIGITS = set("0123456789:-. " + DEG)

# (C name, TTF, capital letter height in pixels, glyph set[, options for build()])
JB = "JetBrainsMono[wght].ttf"
FONTS = [
    ("g_aa7", "DejaVuSans.ttf", 7, None),          # tekstas 128x64 ekrane (pasirinkimas "Smooth")
    ("g_aa10", "DejaVuSans-Bold.ttf", 10, None),   # antrastes prie 5 tasku sriftu (vietoj 3x5/4x5 x2)
    ("g_aa14", "DejaVuSans-Bold.ttf", 14, None),   # antrastes prie 7 tasku srifto, laikrodis 64x32
    ("g_aa21", "DejaVuSans-Bold.ttf", 21, DIGITS), # laikrodis 128x64 (tik skaitmenys)
    # "Smooth mono": JetBrains Mono. 7 tasku aukscio hintingas duoda tik 5 taskus raidei (M, N, W susilieja),
    # todel tekstas piesiamas tikromis proporcijomis ir pritaikomas prie tinklelio - 6 taskai raidei
    ("g_jb7", JB, 7, None, {"wght": 600, "fit": 6}),
    ("g_jb14", JB, 14, None, {"wght": 700, "snap": True}),
    ("g_jb21", JB, 21, DIGITS, {"wght": 700, "snap": True}),
]
LEVEL_MIN = 3        # blankesni uz 3/15 taskai ismetami - kitaip LED'ai aplink raides silpnai svyti
MAX_TOP, MAX_BOTTOM = 2, 4

# Rankomis braižomos M ir W: (plotis taškais, kamieno plotis, įstrižainės plotis horizontaliai,
# M – kiek nusileidžia V, W – iki kur iškyla vidurys, skaičiuojant nuo didžiosios raidės viršaus);
# inset – tarpas kraštuose, kad kamienai būtų tokio pat storio kaip H
MW = {
    "g_aa7":  {"M": (7, 1.0, 1.2, 4.3), "W": (7, 1.0, 1.2, 2.7), "inset": 0.0},
    "g_aa10": {"M": (12, 2.7, 2.6, 7.0), "W": (12, 2.7, 2.6, 3.0), "inset": 0.3},
    "g_aa14": {"M": (16, 3.7, 3.5, 10.0), "W": (17, 3.7, 3.5, 4.0), "inset": 0.3},
}
SS = 16              # braizomos raides piesiamos 16x didesnes ir suvidurkinamos
FIT_SS = 8           # "fit" sriftai piesiami 8x didesni


def ttf_path(name):
    """TTF from ttf/ next to this file (downloaded), otherwise from matplotlib."""
    p = os.path.join(LOCAL_TTF, name)
    return p if os.path.exists(p) else os.path.join(TTF, name)


def load(path, size, wght=None):
    """Font at a size; wght - weight of a variable font (JetBrains Mono: 100..800)."""
    f = ImageFont.truetype(path, size)
    if wght:
        f.set_variation_by_axes([wght])
    return f


def size_for_cap(path, cap, wght=None):
    """Middle of the size range where a hinted 'H' is exactly cap pixels tall."""
    ok = []
    for s10 in range(40, 1200):
        s = s10 / 10
        bb = load(path, s, wght).getbbox("H", anchor="ls")
        h = bb[3] - bb[1]
        if h == cap:
            ok.append(s)
        elif h > cap:
            break
    assert ok, (path, cap)
    return ok[len(ok) // 2]


def render_glyph(font, ch, cap, top, bottom, dx=0.0):
    """dx - horizontal shift by a fraction of a pixel (sharp_glyph)."""
    pad = 8
    adv = int(round(font.getlength(ch)))
    w = adv + 2 * pad + 8
    h = cap + top + bottom
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((pad + dx, top + cap), ch, font=font, fill=255, anchor="ls")
    px = im.load()
    q = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            v = round(px[x, y] * 15 / 255)
            q[y][x] = v if v >= LEVEL_MIN else 0
    cols = [x for x in range(w) if any(q[y][x] for y in range(h))]
    if not cols:
        return {"adv": adv, "x": 0, "w": 0, "rows": []}
    x0, x1 = cols[0], cols[-1]
    return {"adv": adv, "x": x0 - pad, "w": x1 - x0 + 1, "rows": [r[x0:x1 + 1] for r in q]}


def sharp_glyph(font, ch, cap, top, bottom):
    """Simple horizontal hinting: of a few sub-pixel shifts take the one where most ink lands on whole pixels
    (vertical stems crisp instead of two half-lit columns)."""
    best, best_score = None, -1.0
    for dx in (0.0, 0.25, -0.25, 0.5, -0.5):
        g = render_glyph(font, ch, cap, top, bottom, dx)
        ink = sum(v for r in g["rows"] for v in r)
        score = sum(v * v for r in g["rows"] for v in r) / ink if ink else 0.0
        if score > best_score + 0.05:                       # lygiaverciai - maziausias postumis
            best, best_score = g, score
    return best


def fit_size(path, cap, wght):
    """Font size at which an unhinted 'H' is cap pixels tall when drawn FIT_SS times larger."""
    bb = load(path, 100, wght).getbbox("H", anchor="ls")
    return 100 * cap * FIT_SS / (bb[3] - bb[1])


def drop_inner_dot(im):
    """JetBrains Mono '0' has a dot inside - at 7 px it makes 0 look like 8. Keep only the biggest piece of ink."""
    w, h = im.size
    px = im.load()
    seen, pieces = set(), []
    for y0 in range(h):
        for x0 in range(w):
            if px[x0, y0] < 128 or (x0, y0) in seen:
                continue
            piece, todo = [], [(x0, y0)]
            seen.add((x0, y0))
            while todo:
                x, y = todo.pop()
                piece.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen and px[nx, ny] >= 128:
                        seen.add((nx, ny))
                        todo.append((nx, ny))
            pieces.append(piece)
    for piece in sorted(pieces, key=len)[:-1]:
        for x, y in piece:                                   # smulkesni gabalai ir ju krastai - lauk
            for dx in range(-2, 3):
                for dy in range(-2, 3):
                    if 0 <= x + dx < w and 0 <= y + dy < h:
                        px[x + dx, y + dy] = 0
    return im


def fit_glyph(font, ch, cap, top, bottom, adv):
    """Grid fitting for small sizes: the glyph is drawn FIT_SS times larger at its true proportions, then its ink
    is stretched or squeezed to a whole number of pixels (at most adv - 1) so that the left and right edges fall
    on pixel boundaries - outer stems come out crisp. It stays centred where the font puts it in the cell."""
    nat = font.getlength(ch)
    pad = 4 * FIT_SS
    h = (top + cap + bottom) * FIT_SS
    im = Image.new("L", (int(nat) + 2 * pad, h), 0)
    ImageDraw.Draw(im).text((pad, (top + cap) * FIT_SS), ch, font=font, fill=255, anchor="ls")
    if ch == "0":
        im = drop_inner_dot(im)
    bb = im.getbbox()
    if not bb:
        return {"adv": adv, "x": 0, "w": 0, "rows": []}
    x0, x1 = bb[0], bb[2]
    ink = (x1 - x0) / FIT_SS
    n = 1 if ink < 2 else max(1, min(adv - 1, round(ink)))
    sm = im.crop((x0, 0, x1, h)).resize((n * FIT_SS, h), Image.LANCZOS).reduce(FIT_SS)
    px = sm.load()
    rows = []
    for y in range(sm.height):
        r = [round(px[x, y] * 15 / 255) for x in range(n)]
        rows.append([v if v >= LEVEL_MIN else 0 for v in r])
    centre = ((x0 + x1) / 2 - pad) / nat * adv if nat else adv / 2
    return {"adv": adv, "x": int(round(centre - n / 2)), "w": n, "rows": rows}


def drawn_glyph(ch, cap, top, bottom, spec, inset, lead, trail):
    """M or W from four polygons: two stems and two diagonals meeting at height vy (M: from the top corners
    down, W: from the bottom corners up)."""
    w, s, a, vy = spec
    left, right, mid = inset, w - inset, w / 2
    y0 = 0 if ch == "M" else cap
    polys = [[(left, 0), (left + s, 0), (left + s, cap), (left, cap)],
             [(right - s, 0), (right, 0), (right, cap), (right - s, cap)],
             [(left, y0), (left + a, y0), (mid + a / 2, vy), (mid - a / 2, vy)],
             [(right - a, y0), (right, y0), (mid + a / 2, vy), (mid - a / 2, vy)]]
    h = top + cap + bottom
    im = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(im)
    for p in polys:
        d.polygon([(x * SS, (y + top) * SS) for x, y in p], fill=255)
    px = im.reduce(SS).load()
    rows = []
    for y in range(h):
        r = [round(px[x, y] * 15 / 255) for x in range(w)]
        rows.append([v if v >= LEVEL_MIN else 0 for v in r])
    return {"adv": lead + w + trail, "x": lead, "w": w, "rows": rows}


def ink_extent(font, ch, cap, fit=None):
    """Rows of ink above the capital height and below the baseline."""
    g = fit_glyph(font, ch, cap, MAX_TOP, MAX_BOTTOM, fit) if fit else render_glyph(font, ch, cap, MAX_TOP, MAX_BOTTOM)
    rows = [i for i, r in enumerate(g["rows"]) if any(r)]
    if not rows:
        return 0, 0
    return max(0, MAX_TOP - rows[0]), max(0, rows[-1] - (MAX_TOP + cap - 1))


def build(name, ttf, cap, subset, opts=None):
    """opts: {"wght": variable font weight, "snap": sharp_glyph for every glyph,
    "fit": cell width - fit_glyph for every glyph (monospace, small sizes)}"""
    opts = opts or {}
    path = ttf_path(ttf)
    fit = opts.get("fit")
    size = fit_size(path, cap, opts.get("wght")) if fit else size_for_cap(path, cap, opts.get("wght"))
    font = load(path, size, opts.get("wght"))
    chars = [c for c in ALL if subset is None or c in subset]
    top = max(ink_extent(font, c, cap, fit)[0] for c in chars)
    bottom = max(ink_extent(font, c, cap, fit)[1] for c in chars)
    space_adv = fit or int(round(font.getlength(" ")))
    h = render_glyph(font, "H", cap, top, bottom)          # braizomoms M ir W - tokie pat tarpai kaip H
    glyphs, data = [], []
    for ch in ALL:
        if ch not in chars:
            glyphs.append({"adv": space_adv, "x": 0, "w": 0, "off": 0, "ch": ch, "rows": []})
            continue
        if name in MW and ch in MW[name]:
            g = drawn_glyph(ch, cap, top, bottom, MW[name][ch], MW[name]["inset"], h["x"], h["adv"] - h["x"] - h["w"])
        elif fit:
            g = fit_glyph(font, ch, cap, top, bottom, fit)
        elif opts.get("snap"):
            g = sharp_glyph(font, ch, cap, top, bottom)
        else:
            g = render_glyph(font, ch, cap, top, bottom)
        g["off"] = len(data)
        g["ch"] = ch
        for r in g["rows"]:
            for i in range(0, g["w"], 2):
                hi = r[i]
                lo = r[i + 1] if i + 1 < g["w"] else 0
                data.append(hi << 4 | lo)
        glyphs.append(g)
    return {"name": name, "ttf": ttf, "size": size / FIT_SS if fit else size, "cap": cap, "top": top, "bottom": bottom,
            "wght": opts.get("wght"), "glyphs": glyphs, "data": data}


LICENSE = """ * DejaVu fonts: Fonts are (c) Bitstream (see below). DejaVu changes are in public domain.
 * Bitstream Vera Fonts Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark
 * of Bitstream, Inc. Permission is hereby granted, free of charge, to any person obtaining a copy of the fonts
 * accompanying this license ("Fonts") and associated documentation files (the "Font Software"), to reproduce and
 * distribute the Font Software, including without limitation the rights to use, copy, merge, publish, distribute,
 * and/or sell copies of the Font Software, and to permit persons to whom the Font Software is furnished to do so,
 * subject to the following conditions: The above copyright and trademark notices and this permission notice shall
 * be included in all copies of one or more of the Font Software typefaces. The Font Software may be modified,
 * altered, or added to, and in particular the designs of glyphs or characters in the Fonts may be modified and
 * additional glyphs or characters may be added to the Fonts, only if the fonts are renamed to names not
 * containing either the words "Bitstream" or the word "Vera". The Font Software may be sold as part of a larger
 * software package but no copy of one or more of the Font Software typefaces may be sold by itself.
 * THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT,
 * PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT
 * SOFTWARE OR FROM OTHER DEALINGS IN THE FONT SOFTWARE. Full text: LICENSE_DEJAVU in matplotlib's mpl-data/fonts/ttf.
 *
 * JetBrains Mono (g_jb*): Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono).
 * This Font Software is licensed under the SIL Open Font License, Version 1.1 (https://openfontlicense.org);
 * full text: components/gfx/ttf/OFL.txt. Rendered to bitmaps here (grid-fitted, '0' without the inner dot at 7 px)."""


def label(ch):
    return {" ": "space", '"': "dquote", "'": "apostrophe", "\\": "backslash", "/": "slash", "*": "star",
            DEG: "degree"}.get(ch, ch)


def emit(fonts, path):
    out = ["/* Glotnus (anti-aliased) sriftai - sugeneruota fontgen_aa.py, rankomis nekeisti.",
           " * 4 bitai taskui (0..15), eilute - (w+1)/2 baitu, kairysis taskas - vyresnysis pusbaitis.",
           " *", LICENSE, " */", '#include "font.h"', ""]
    for f in fonts:
        n = f["name"]
        wght = f" wght {f['wght']}" if f.get("wght") else ""
        out.append(f"/* {f['ttf']}{wght} {f['size']:.1f} px: didzioji raide {f['cap']}, virs jos {f['top']}, "
                   f"po linija {f['bottom']} */")
        out.append(f"static const uint8_t {n}_data[{max(1, len(f['data']))}] = {{")
        d = f["data"] or [0]
        for i in range(0, len(d), 24):
            out.append("    " + ", ".join(f"0x{v:02X}" for v in d[i:i + 24]) + ",")
        out.append("};")
        out.append(f"static const aa_glyph_t {n}_glyphs[65] = {{")
        for g in f["glyphs"]:
            out.append(f"    {{{g['off']}, {g['w']}, {g['x']}, {g['adv']}}}, /* {label(g['ch'])} */")
        out.append("};")
        out.append(f"const aa_font_t {n} = {{ {f['cap']}, {f['top']}, {f['top'] + f['cap'] + f['bottom']}, "
                   f"{n}_glyphs, {n}_data }};")
        out.append("")
    with open(path, "w", newline="\n") as fh:
        fh.write("\n".join(out))


def preview(fonts, path):
    """LED dot preview of each font (8 px per LED) - for checking by eye."""
    p = 8
    text = "AEROBOX RYR1234 VNO>DUB FL370 12:45 13" + DEG + "C"
    rows = []
    for f in fonts:
        h = f["top"] + f["cap"] + f["bottom"]
        x, cells = 0, {}
        for ch in text:
            g = f["glyphs"][ALL.index(ch) if ch in ALL else ALL.index("?")]
            for r, row in enumerate(g["rows"]):
                for c, a in enumerate(row):
                    if a:
                        cells[(x + g["x"] + c, r)] = a
            x += g["adv"]
        im = Image.new("RGB", ((x + 2) * p, h * p), (5, 5, 6))
        d = ImageDraw.Draw(im)
        for yy in range(h):
            for xx in range(x + 2):
                a = cells.get((xx, yy), 0) / 15
                r = p * 0.42 if a else p * 0.23
                col = (int(255 * a), int(140 * a), 0) if a else (23, 23, 28)
                d.ellipse([xx * p + p / 2 - r, yy * p + p / 2 - r, xx * p + p / 2 + r, yy * p + p / 2 + r], fill=col)
        rows.append(im)
    sheet = Image.new("RGB", (max(i.width for i in rows), sum(i.height + 8 for i in rows)), (0, 0, 0))
    y = 0
    for i in rows:
        sheet.paste(i, (0, y))
        y += i.height + 8
    sheet.save(path)


if __name__ == "__main__":
    fonts = [build(*f) for f in FONTS]
    emit(fonts, "font_aa.c")
    for f in fonts:
        print(f"{f['name']}: {f['ttf']} {f['size']:.1f}px cap {f['cap']} top {f['top']} bottom {f['bottom']} "
              f"data {len(f['data'])} B")
    if len(sys.argv) > 1:
        preview(fonts, sys.argv[1])
    print("ok")
