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

# ============ fixed 9x18 metrics and 4-bit coverage ============

WIDTH, HEIGHT, SIZE, BASELINE, THRESHOLD = 9, 18, 15.36, 14, 9
SUPERSAMPLE = 8
MARGIN = 2

def raster():
    source = MODULE / 'font/source/CascadiaMono-Regular.ttf'
    origin = json.loads((source.parent / 'origin.json').read_text())
    if hashlib.sha256(source.read_bytes()).hexdigest().upper() != origin['SHA256']:
        raise ValueError('Source font hash mismatch')
    font = ImageFont.truetype(source, SIZE * SUPERSAMPLE, layout_engine=ImageFont.Layout.BASIC)
    glyphs = {}
    for scalar in SCALARS:
        ch = chr(scalar)
        if abs(font.getlength(ch) / SUPERSAMPLE - WIDTH) > 0.02:
            raise ValueError(f'Glyph advance is not {WIDTH}: {scalar:x}')
        # Wider canvas than the cell: ink that would leave the cell must be detected, not clipped.
        big = Image.new('L', ((WIDTH + 2 * MARGIN) * SUPERSAMPLE, (HEIGHT + 2 * MARGIN) * SUPERSAMPLE))
        ImageDraw.Draw(big).text((MARGIN * SUPERSAMPLE, (MARGIN + BASELINE) * SUPERSAMPLE), ch, font=font, fill=255, anchor='ls')
        image = big.resize((WIDTH + 2 * MARGIN, HEIGHT + 2 * MARGIN), Image.BOX)
        ink = [(x, y) for y in range(image.height) for x in range(image.width) if image.getpixel((x, y)) >= THRESHOLD]
        if any(not MARGIN <= x < MARGIN + WIDTH or not MARGIN <= y < MARGIN + HEIGHT for x, y in ink):
            raise ValueError(f'Glyph does not fit {WIDTH}x{HEIGHT} cell: {scalar:x}')
        rows = [sum(((image.getpixel((MARGIN+x,MARGIN+y))*15+127)//255) << (4*x)
                    for x in range(WIDTH)) for y in range(HEIGHT)]
        if scalar != 32 and not any(rows):
            raise ValueError(f'Empty glyph: {scalar:x}')
        glyphs[scalar] = rows
    if any(rows == glyphs[65533] for scalar, rows in glyphs.items() if scalar != 65533):
        raise ValueError('Unexpected missing glyph')
    return glyphs

# ============ grayscale reference ============

def color(row, x, foreground=(255,255,255), background=(8,16,24)):
    alpha = (row >> (4*x)) & 15
    return tuple((f*alpha+b*(15-alpha)+7)//15 for f,b in zip(foreground,background))

# ============ generated licensed bitmap data ============

def generated(glyphs):
    lines = ['#include "font.h"', 'static const BOOT_U64 coverage[][18] = {']
    for rows in glyphs.values():
        lines.append('    {' + ','.join(f'0x{v:x}ULL' for v in rows) + '},')
    lines += ['};', 'static const X86_FB_GLYPH glyphs[] = {']
    binary = lambda rows: [sum((1 << (WIDTH-1-x)) for x in range(WIDTH) if (v >> (4*x)) & 15 >= 8) for v in rows]
    for scalar, rows in glyphs.items():
        values = ','.join(f'0x{v:03x}' for v in binary(rows))
        lines.append(f'    {{0x{scalar:x}, {{{values}}}}},')
    missing = ','.join(f'0x{v:03x}' for v in binary(glyphs[65533]))
    lines += ['};', f'static const X86_FB_FONT font = {{glyphs, {len(glyphs)}, sizeof(glyphs),',
        f'    {{{missing}}}, {HEIGHT}, {WIDTH}}};', '', '// ====== x86_fb_console_font ======', '',
        'const X86_FB_FONT *x86_fb_console_font(void)', '{', '    return &font;', '}', '']
    lines += [r"""
// ====== x86_fb_font_row ======

BOOT_U64 x86_fb_font_row(const X86_FB_FONT *input, BOOT_U32 glyph, BOOT_U32 row)
{
    BOOT_U64 packed = 0;
    const unsigned short *bitmap;
    if (!input || row >= input->Height || row >= X86_FB_CELL_HEIGHT ||
        (input->Width != 8 && input->Width != X86_FB_CELL_WIDTH)) return 0;
    if (input->Glyphs == font.Glyphs && input->Count == font.Count &&
        input->Bytes == font.Bytes && input->Height == font.Height && input->Width == font.Width) {
        if (glyph < font.Count) return coverage[glyph][row];
        if (input->Missing[row] == font.Missing[row]) return coverage[font.Count-1][row];
    }
    bitmap = glyph < input->Count ? input->Glyphs[glyph].Rows : input->Missing;
    for (BOOT_U32 x = 0; x < input->Width; ++x)
        if (bitmap[row] & (1U << (input->Width-1-x))) packed |= 15ULL << (4*x);
    return packed;
}

// ====== x86_fb_text_color ======

BOOT_U32 x86_fb_text_color(BOOT_U64 row, BOOT_U32 x, BOOT_U32 foreground, BOOT_U32 background)
{
    BOOT_U32 result = 0, alpha;
    if (x >= X86_FB_CELL_WIDTH) return background;
    alpha = (BOOT_U32)(row >> (4*x)) & 15U;
    for (BOOT_U32 shift = 0; shift < 24; shift += 8) {
        BOOT_U32 f = (foreground >> shift) & 255U, b = (background >> shift) & 255U;
        result |= ((f*alpha+b*(15-alpha)+7)/15) << shift;
    }
    return result;
}
"""]
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
