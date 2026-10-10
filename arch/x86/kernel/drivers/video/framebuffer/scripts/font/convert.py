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

def raster():
    source = MODULE / 'font/source/CascadiaMono-Regular.ttf'
    origin = json.loads((source.parent / 'origin.json').read_text())
    if hashlib.sha256(source.read_bytes()).hexdigest().upper() != origin['SHA256']:
        raise ValueError('Source font hash mismatch')
    font = ImageFont.truetype(source, 13, layout_engine=ImageFont.Layout.BASIC)
    glyphs = {}
    for scalar in SCALARS:
        ch = chr(scalar)
        left, top, right, bottom = font.getbbox(ch, anchor='ls')
        if left < 0 or top < -12 or right > 8 or bottom > 4 or font.getlength(ch) != 8:
            raise ValueError(f'Glyph does not fit 8x16 cell: {scalar:x}')
        image = Image.new('L', (8, 16))
        ImageDraw.Draw(image).text((0, 12), ch, font=font, fill=255, anchor='ls')
        rows = [sum((0x80 >> x) for x in range(8) if image.getpixel((x, y)) >= 128) for y in range(16)]
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
        values = ','.join(f'0x{v:02x}' for v in rows)
        lines.append(f'    {{0x{scalar:x}, {{{values}}}}},')
    missing = ','.join(f'0x{v:02x}' for v in glyphs[65533])
    lines += ['};', f'static const X86_FB_FONT font = {{glyphs, {len(glyphs)}, sizeof(glyphs),',
        f'    {{{missing}}}, 16}};', '', '// ====== x86_fb_console_font ======', '',
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
    print(f'PASS: {len(glyphs)} glyphs 8x16; Pillow {PIL.__version__}; FreeType {ImageFont.core.freetype2_version}')


if __name__ == '__main__':
    main()
