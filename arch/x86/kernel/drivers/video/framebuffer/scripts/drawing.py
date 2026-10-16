import importlib.util
from pathlib import Path

# ============ independent expected raster ============

def cases(console=False):
    pixels = {}
    for x, width, color in [(16, 64, (32, 64, 96)), (80, 32, (208, 144, 48))]:
        for y in range(16, 32):
            for column in range(x, x + width):
                pixels[column, y] = color
    glyphs = [
        [0x3c,0x42,0x42,0x42,0x42,0x42,0x3c,0],
        [0x3c,0x42,0x40,0x3c,0x02,0x42,0x3c,0],
        [0,0x18,0x18,0,0x18,0x18,0,0],
        [0,0,0,0,0,0,0,0],
        [0x10,0x08,0x00,0x3c,0x02,0x3e,0x42,0x3e],
        [0x10,0x08,0x00,0x3c,0x42,0x7e,0x40,0x3c],
        [0x08,0x10,0x00,0x3c,0x42,0x7e,0x40,0x3c],
        [0x20,0x10,0x00,0x30,0x10,0x10,0x10,0x38],
        [0x10,0x08,0x00,0x3c,0x42,0x42,0x42,0x3c],
        [0x10,0x08,0x00,0x42,0x42,0x42,0x46,0x3a],
        [0xff,0x81,0x81,0x81,0x81,0x81,0x81,0xff]]
    for cell, rows in enumerate(glyphs):
        for row, bitmap in enumerate(rows):
            for column in range(8):
                pixels[16 + cell * 8 + column, 40 + row] = (
                    (255, 255, 255) if bitmap & (0x80 >> column) else (8, 16, 24))
    spec = importlib.util.spec_from_file_location('font_converter', Path(__file__).parent / 'font/convert.py')
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    bitmap = converter.raster()
    width, height = converter.WIDTH, converter.HEIGHT
    for y, text in [(64, 'MikeSW OS: àèéìòù €'), (88, ''.join(map(chr, range(32, 64)))),
        (108, ''.join(map(chr, range(64, 96)))), (128, ''.join(map(chr, range(96, 127))))]:
        for cell, char in enumerate(text):
            for row, mask in enumerate(bitmap[ord(char)]):
                for col in range(width):
                    pixels[16 + cell * width + col, y + row] = (
                        (255, 255, 255) if mask & (1 << (width - 1 - col)) else (8, 16, 24))
    if console:
        # The guest draws the console probe at grid cell (2, 10).
        for line, text in enumerate(['ciao è €', 'prova AC', 'Tutto OK🙂']):
            for cell, char in enumerate(text.ljust(24)):
                for row, mask in enumerate(bitmap.get(ord(char), bitmap[65533])):
                    for col in range(width):
                        pixels[2 * width + cell * width + col, 10 * height + line * height + row] = (
                            (255, 255, 255) if mask & (1 << (width - 1 - col)) else (8, 16, 24))
    return pixels

# ============ physical color and reserved byte ============

def verify_physical(read, vm, monitor, base, stride, pixel_format, console=False):
    expected = cases(console)
    for y in sorted({y for x, y in expected}):
        xs = sorted(x for x, row in expected if row == y)
        start = base + (y * stride + xs[0]) * 4
        end = base + (y * stride + xs[-1] + 1) * 4
        aligned = start & ~7
        words = read(vm, monitor, aligned, (end - aligned + 7) // 8)
        for x in xs:
            address = base + (y * stride + x) * 4
            value = (words[(address - aligned) // 8] >> ((address & 7) * 8)) & 0xffffffff
            r, g, b = expected[x, y]
            color = (r | (g << 8) | (b << 16)) if pixel_format == 0 else (b | (g << 8) | (r << 16))
            if value != color:
                raise RuntimeError(f'Physical drawing mismatch at {x},{y}')
    return len(expected)

# ============ every visible rectangle and glyph pixel ============

def verify_visible(image, width, console=False):
    expected = cases(console)
    for (x, y), color in expected.items():
        offset = (y * width + x) * 3
        if image[offset:offset + 3] != bytes(color):
            raise RuntimeError(f'Visible drawing mismatch at {x},{y}')
    return len(expected)
