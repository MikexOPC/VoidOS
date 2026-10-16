from pathlib import Path
ROOT = Path(__file__).resolve().parents[5]
SIZE = 18

# ============ sharp arrow after the user's reference image ============

# Source: scripts/pointer/left_ptr.svg (user's cursor, 16 October 2026), rasterized to 18 rows by
# scripts/pointer/svg_art.py (16x supersampling, stroke 17/256 of the height, 50% thresholds).
# X = grey fill (the reference black is invisible on the dark console), o = white outline,
# . = transparent. No antialiasing: every sample is
# grey (6 << 5), fully white (16 << 5) or transparent (0). Hotspot: the tip at (1,0).
ART = [
    'oooo..............',
    'oooooo............',
    'ooXXooo...........',
    'oXXXXooo..........',
    'oXXXXXooo.........',
    'oXXXXXXooo........',
    'oXXXXXXXooo.......',
    'oXXXXXXXXooo......',
    'oXXXXXXXXXooo.....',
    'oXXXXXXXXXXXoo....',
    'oXXXXXXXXXXXXo....',
    'oXXXXXXXXXXXXo....',
    'oXXXXXXXXXXooo....',
    'oXXXXXoooooooo....',
    'oXXXXooo..........',
    'ooXXooo...........',
    'oooooo............',
    '.oooo.............',
]
LEVEL = {'.': 0, 'X': 6 << 5, 'o': 16 << 5}

def raster():
    if len(ART) != SIZE or any(len(row) != SIZE for row in ART):
        raise SystemExit('Pointer art must be %dx%d' % (SIZE, SIZE))
    return [[LEVEL[cell] for cell in row] for row in ART]

# ============ fixed production asset and independent test reference ============

def main():
    rows = raster()
    table = '\n'.join('    {'+','.join(map(str,row))+'},' for row in rows)
    source = '''#include "shape.h"
static const uint16_t coverage[18][18] = {
TABLE
};

// ====== kernel_pointer_sample ======

uint32_t kernel_pointer_sample(uint32_t x, uint32_t y)
{
    return x < 18 && y < 18 ? coverage[y][x] : 0;
}

// ====== kernel_pointer_blend ======

uint32_t kernel_pointer_blend(uint32_t sample, uint32_t background)
{
    uint32_t black = sample & 31U, white = sample >> 5, result = 0;
    if (black + white > 16 || background & 0xff000000U) return background;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        uint32_t channel = (background >> shift) & 255U;
        channel = (channel * (16 - black - white) + 255 * white + 8) / 16;
        result |= channel << shift;
    }
    return result;
}
'''.replace('TABLE',table)
    (ROOT/'kernel/console/video/pointer/shape.c').write_text(source,encoding='utf-8',newline='\n')
    reference = ROOT/'kernel/console/video/scripts/pointer/firmware.py'
    text = reference.read_text(encoding='utf-8')
    start = text.index('COVERAGE =') if 'COVERAGE =' in text else text.index('BOUNDS =')
    marker = '# Console cell in pixels'
    end = text.index(marker) if marker in text else text.index('def expected_position')
    values = '\n'.join('    '+repr(row)+',' for row in rows)
    replacement = 'COVERAGE = [\n'+values+'\n]\n\n# ============ bitmap coverage reference ============\n\n'
    replacement += 'def sample(x, y):\n    return COVERAGE[y][x] if 0 <= x < 18 and 0 <= y < 18 else 0\n\n'
    replacement += 'def blend(pixel, rgb):\n    black, white = pixel & 31, pixel >> 5\n    return bytes((c*(16-black-white)+255*white+8)//16 for c in rgb)\n\n'
    reference.write_text(text[:start]+replacement+text[end:],encoding='utf-8',newline='\n')
    try:
        from PIL import Image
    except ImportError:
        return
    preview = Image.new('RGB',(SIZE,SIZE),(8,16,24))
    for y in range(SIZE):
        for x in range(SIZE):
            b,w = rows[y][x]&31,rows[y][x]>>5
            preview.putpixel((x,y),tuple((c*(16-b-w)+255*w+8)//16 for c in (8,16,24)))
    preview.resize((SIZE*12,SIZE*12),Image.NEAREST).save(ROOT/'kernel/build/pointer-sharp-preview.png')

if __name__ == '__main__':
    main()
