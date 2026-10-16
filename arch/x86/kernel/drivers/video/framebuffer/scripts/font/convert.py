import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import PIL
MODULE = Path(__file__).resolve().parents[2]
SCALARS = sorted(list(range(32, 127)) + [192, 200, 201, 204, 210, 217,
    224, 232, 233, 236, 242, 249, 8364, 65533])

# ============ bounded offline rasterization ============

# 10x20 cell: Cascadia Mono advance is 0.586 em, so a 17 px em gives exactly 10 px;
# ascent 16 and descent 4 fill the 20 rows with the baseline on row 16 (row 0 is the top).
WIDTH, HEIGHT, SIZE, BASELINE, THRESHOLD = 10, 20, 17, 16, 100
MARGIN = 2

def raster():
    source = MODULE / 'font/source/CascadiaMono-Regular.ttf'
    origin = json.loads((source.parent / 'origin.json').read_text())
    if hashlib.sha256(source.read_bytes()).hexdigest().upper() != origin['SHA256']:
        raise ValueError('Source font hash mismatch')
    font = ImageFont.truetype(source, SIZE, layout_engine=ImageFont.Layout.BASIC)
    glyphs = {}
    for scalar in SCALARS:
        ch = chr(scalar)
        if font.getlength(ch) != WIDTH:
            raise ValueError(f'Glyph advance is not {WIDTH}: {scalar:x}')
        # Wider canvas than the cell: ink that would leave the cell must be detected, not clipped.
        image = Image.new('L', (WIDTH + 2 * MARGIN, HEIGHT + 2 * MARGIN))
        ImageDraw.Draw(image).text((MARGIN, MARGIN + BASELINE), ch, font=font, fill=255, anchor='ls')
        ink = [(x, y) for y in range(image.height) for x in range(image.width) if image.getpixel((x, y)) >= THRESHOLD]
        if any(not MARGIN <= x < MARGIN + WIDTH or not MARGIN <= y < MARGIN + HEIGHT for x, y in ink):
            raise ValueError(f'Glyph does not fit {WIDTH}x{HEIGHT} cell: {scalar:x}')
        rows = [sum((1 << (WIDTH - 1 - x)) for x in range(WIDTH) if image.getpixel((MARGIN + x, MARGIN + y)) >= THRESHOLD)
                for y in range(HEIGHT)]
        if scalar != 32 and not any(rows):
            raise ValueError(f'Empty glyph: {scalar:x}')
        glyphs[scalar] = rows
    if any(rows == glyphs[65533] for scalar, rows in glyphs.items() if scalar != 65533):
        raise ValueError('Unexpected missing glyph')
    return glyphs

# ============ generated licensed bitmap data ============

def generated(glyphs):
    lines = ['#include "font.h"', 'static const X86_FB_GLYPH glyphs[] = {']
    for scalar, rows in glyphs.items():
        values = ','.join(f'0x{v:03x}' for v in rows)
        lines.append(f'    {{0x{scalar:x}, {{{values}}}}},')
    missing = ','.join(f'0x{v:03x}' for v in glyphs[65533])
    lines += ['};', f'static const X86_FB_FONT font = {{glyphs, {len(glyphs)}, sizeof(glyphs),',
        f'    {{{missing}}}, {HEIGHT}, {WIDTH}}};', '', '// ====== x86_fb_console_font ======', '',
        'const X86_FB_FONT *x86_fb_console_font(void)', '{', '    return &font;', '}', '']
    return '\n'.join(lines)

# ============ source verification or regeneration ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    glyphs = raster()
    target = MODULE / 'font/font.c'
    content = generated(glyphs)
    if args.check:
        if target.read_text() != content:
            raise ValueError('Regenerated font differs: check Pillow/FreeType versions')
    else:
        target.write_text(content, encoding='utf-8', newline='\n')
    print(f'PASS: {len(glyphs)} glyphs {WIDTH}x{HEIGHT}; Pillow {PIL.__version__}; FreeType {ImageFont.core.freetype2_version}')


if __name__ == '__main__':
    main()
