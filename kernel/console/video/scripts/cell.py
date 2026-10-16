import importlib.util
from pathlib import Path
import re
import sys
import time
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
PHASES = ('GATE', 'PLAN', 'INVENTORY', 'PIXELS', 'AUTHORITY', 'RETURN')

# ============ measured campaign invariants ============

def validate(record):
    cells = record.get('CELLS', 0)
    size, groups = record.get('BATCH_SIZE', 0), record.get('GROUPS', 0)
    if type(size) is not int or size not in (1, 8) or type(groups) is not int or \
            groups < 1 or groups * size != cells or record.get('SLOW_CELLS', -1) % size:
        raise RuntimeError('Invalid group size or cell accounting')
    if not 32 <= cells <= 65536 or record.get('PM_LIMIT') != 357955 or \
            record.get('PM_GOAL') != 35796 or not 0 <= record.get('SLOW_CELLS', -1) <= cells or \
            (record.get('MAX_PM', 0) > 35796) != (record.get('SLOW_CELLS', 0) > 0) or \
            not 0 < record.get('MAX_PM', 0) <= 357955 or \
            record.get('PM_MASK') not in (0xffffff, 0xffffffff) or \
            record.get('LOSSES') != 0 or not record.get('IRQS', 0) or \
            record.get('HPET_END', 0) <= record.get('HPET_START', 0) or \
            record.get('LAST_SCALAR') != (0x41, 0xe8, 0x20ac)[(cells - 1) % 3]:
        raise RuntimeError('Insufficient or inconsistent measured draw evidence')
    total = record.get('PROFILE_TOTAL', 0)
    if type(total) is not int or not 0 < total <= groups * record['MAX_PM']:
        raise RuntimeError('Invalid profile total')
    accumulated = 0
    for phase in PHASES:
        ticks, maximum = record.get(phase + '_SUM', -1), record.get(phase + '_MAX', -1)
        if type(ticks) is not int or type(maximum) is not int or \
                not 0 <= maximum <= record['MAX_PM'] or not maximum <= ticks <= groups * maximum:
            raise RuntimeError('Invalid phase accounting: ' + phase)
        accumulated += ticks
    if accumulated != total:
        raise RuntimeError('Phase sums differ from profile total')

# ============ official-font raster and owned VM physical reader ============

def verify(vm, scenario, log):
    if 'KERNEL_CONSOLE_DRAW_ACTIVE_OK' not in log:
        raise RuntimeError('Missing measured draw completion')
    number = lambda field: int(re.search(re.escape(field) + r': 0x([0-9a-fA-F]{16})', log)[1], 16)
    record = {name: number('KERNEL_CONSOLE_DRAW_' + name) for name in
        ['CELLS', 'BATCH_SIZE', 'GROUPS', 'MAX_PM', 'PM_LIMIT', 'PM_GOAL', 'SLOW_CELLS', 'PM_MASK', 'LAST_SCALAR', 'LOSSES', 'IRQS', 'HPET_START', 'HPET_END']}
    record['PROFILE_TOTAL'] = number('KERNEL_CONSOLE_PROFILE_TOTAL')
    for phase in PHASES:
        for suffix in ('SUM', 'MAX'):
            record[phase + '_' + suffix] = number('KERNEL_CONSOLE_PROFILE_' + phase + '_' + suffix)
    validate(record)
    scripts = ROOT / 'arch/x86/kernel/drivers/video/framebuffer/scripts'
    sys.path.insert(0, str(scripts))
    spec = importlib.util.spec_from_file_location('live_fb', scripts / 'firmware.py')
    framebuffer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(framebuffer)
    spec = importlib.util.spec_from_file_location('font_raster', scripts / 'font/convert.py')
    font = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(font)
    size = record['BATCH_SIZE']
    raster = font.raster()
    glyphs = [raster[(0x41, 0xe8, 0x20ac)[(record['CELLS'] - size + index) % 3]] for index in range(size)]
    base, stride, pixel_format, width, height = [number('KERNEL_FB_' + name)
        for name in ['BASE', 'STRIDE', 'FORMAT', 'WIDTH', 'HEIGHT']]
    if pixel_format not in (0, 1) or width < 2 * font.WIDTH + size * font.WIDTH or height < 10 * font.HEIGHT + font.HEIGHT or stride < width:
        raise RuntimeError('Unsupported measured cell geometry')
    # The guest draws the measured cells from grid cell (2, 10).
    left, top = 2 * font.WIDTH, 10 * font.HEIGHT
    for row in range(font.HEIGHT):
        words = framebuffer.physical(vm, scenario / 'monitor.log', base + ((top + row) * stride + left) * 4, (font.WIDTH * size + 1) // 2)
        for column in range(font.WIDTH * size):
            bitmap = glyphs[column // font.WIDTH][row]
            r, g, b = (255, 255, 255) if bitmap & (1 << (font.WIDTH - 1 - column % font.WIDTH)) else (8, 16, 24)
            expected = r | (g << 8) | (b << 16) if pixel_format == 0 else b | (g << 8) | (r << 16)
            if (words[column // 2] >> ((column % 2) * 32)) & 0xffffffff != expected:
                raise RuntimeError('Measured physical cell pixel mismatch')
    image = scenario / 'screen.ppm'
    vm.stdin.write('screendump "' + image.as_posix() + '"\n')
    vm.stdin.flush()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        data = image.read_bytes() if image.exists() else b''
        header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
        if header and len(data) - header.end() == width * height * 3:
            break
        if vm.poll() is not None:
            raise RuntimeError('Owned VM exited before screenshot')
        time.sleep(0.05)
    else:
        raise TimeoutError('Incomplete measured cell screenshot')
    if (int(header[1]), int(header[2])) != (width, height):
        raise RuntimeError('Measured screenshot geometry mismatch')
    pixels = data[header.end():]
    for row in range(font.HEIGHT):
        for column in range(font.WIDTH * size):
            bitmap = glyphs[column // font.WIDTH][row]
            expected = bytes((255, 255, 255) if bitmap & (1 << (font.WIDTH - 1 - column % font.WIDTH)) else (8, 16, 24))
            offset = ((top + row) * width + left + column) * 3
            if pixels[offset:offset + 3] != expected:
                raise RuntimeError('Measured visible cell pixel mismatch')
    record.update({'PhysicalPixels': font.WIDTH * font.HEIGHT * size, 'VisiblePixels': font.WIDTH * font.HEIGHT * size})
    return record
