from pathlib import Path
from PIL import Image, ImageDraw
ROOT = Path(__file__).resolve().parents[5]

# ============ original rounded arrow, four by four coverage ============

def raster():
    points = [(4.7, 2.8)]
    def curve(a, b, end):
        start = points[-1]
        for step in range(1, 33):
            t = step / 32
            points.append(tuple((1-t)**3*start[k]+3*(1-t)**2*t*a[k]+
                                3*(1-t)*t*t*b[k]+t**3*end[k] for k in range(2)))
    curve((2,1),(1.5,2),(2.2,4.6))
    points.append((7.7,20.4))
    curve((8.2,22.5),(9.7,22.6),(10.5,20.5))
    points.append((12.9,15.1))
    curve((13.4,13.8),(14,13.1),(15.5,12.7))
    points.append((20.8,10.9))
    curve((23,10.2),(23,8.7),(20.9,7.9))
    points.append(points[0])
    image = Image.new('L',(96,96))
    draw = ImageDraw.Draw(image)
    scaled = [(round(x*4),round(y*4)) for x,y in points]
    draw.polygon(scaled,fill=2)
    draw.line(scaled,fill=1,width=8,joint='curve')
    return [[sum((1 if image.getpixel((x*4+dx,y*4+dy)) == 1 else
                  32 if image.getpixel((x*4+dx,y*4+dy)) == 2 else 0)
                 for dy in range(4) for dx in range(4)) for x in range(24)] for y in range(24)]

# ============ fixed production asset and independent test reference ============

def main():
    rows = raster()
    table = '\n'.join('    {'+','.join(map(str,row))+'},' for row in rows)
    source = '''#include "shape.h"
static const uint16_t coverage[24][24] = {
TABLE
};

// ====== kernel_pointer_sample ======

uint32_t kernel_pointer_sample(uint32_t x, uint32_t y)
{
    return x < 24 && y < 24 ? coverage[y][x] : 0;
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
    end = text.index('def expected_position')
    values = '\n'.join('    '+repr(row)+',' for row in rows)
    replacement = 'COVERAGE = [\n'+values+'\n]\n\n# ============ bitmap coverage reference ============\n\n'
    replacement += 'def sample(x, y):\n    return COVERAGE[y][x] if 0 <= x < 24 and 0 <= y < 24 else 0\n\n'
    replacement += 'def blend(pixel, rgb):\n    black, white = pixel & 31, pixel >> 5\n    return bytes((c*(16-black-white)+255*white+8)//16 for c in rgb)\n\n'
    reference.write_text(text[:start]+replacement+text[end:],encoding='utf-8',newline='\n')
    preview = Image.new('RGB',(24,24),(8,16,24))
    for y in range(24):
        for x in range(24):
            b,w = rows[y][x]&31,rows[y][x]>>5
            preview.putpixel((x,y),tuple((c*(16-b-w)+255*w+8)//16 for c in (8,16,24)))
    preview.resize((192,192)).save(ROOT/'kernel/build/pointer-rounded-preview.png')

if __name__ == '__main__':
    main()
