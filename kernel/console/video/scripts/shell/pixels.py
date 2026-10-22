import importlib.util
from pathlib import Path
import re
import sys
import time
ROOT = Path(__file__).resolve().parents[5]

# ============ independent terminal reference ============

def expected(log, columns, rows):
    cells, x, y = [32] * (columns * rows), 0, 0
    operations = []
    for line in log.splitlines():
        if line.startswith('PS2_LINE_READY: '):
            operations.append('> ' + line[len('PS2_LINE_READY: '):] + '\n')
        else:
            match = re.fullmatch(r'(?:\[[^]]+\]\s+)?(SHELL_(?:HELP:.*|EMPTY|BAD_ARGUMENTS|'
                r'UNKNOWN_COMMAND|STATUS:.*|EXIT_REQUEST|LINE_REJECTED_TRUNCATED|ECHO:.*|INPUT_(?:BYTES|KEYS|LINES):.*))', line)
            if match:
                operations.append(match[1] + '\n')
    operations.append('> ')
    for text in operations:
        for char in text:
            if char == '\n':
                x, y = 0, y + 1
            else:
                if x == columns:
                    x, y = 0, y + 1
                if y == rows:
                    cells = cells[columns:] + [32] * columns
                    y -= 1
                cells[y * columns + x] = ord(char)
                x += 1
            if y == rows:
                cells = cells[columns:] + [32] * columns
                y -= 1
    return cells, y * columns + min(x, columns - 1)

# ============ independent physical and visible pixels ============

def accounting(log):
    values = []
    for field in ['CELLS', 'GROUPS', 'MAX_PM', 'OVERRUNS']:
        match = re.search('KERNEL_CONSOLE_SHELL_' + field + r': 0x([0-9a-fA-F]{16})', log)
        if not match:
            raise RuntimeError('Missing renderer accounting: ' + field)
        values.append(int(match[1], 16))
    cells_done, groups, maximum, overruns = values
    if (not 0 < groups <= cells_done <= 8 * groups or not 0 < maximum <= 0xffffffff or
        overruns > groups or bool(overruns) != (maximum > 357955)):
        raise RuntimeError('Invalid bounded renderer accounting')
    return values

# ============ verify ============

def verify(vm, scenario, log):
    if 'KERNEL_CONSOLE_SHELL_CLEAN' not in log or 'KERNEL_CONSOLE_SHELL_OPEN' not in log:
        raise RuntimeError('Missing operational console lifetime evidence')
    number = lambda field: int(re.search(re.escape(field) + r': 0x([0-9a-fA-F]{16})', log)[1], 16)
    cells_done, groups, maximum, overruns = accounting(log)
    if overruns and 'KERNEL_CONSOLE_SLOW_GROUP' not in log:
        raise RuntimeError('Missing latency warning')
    scripts = ROOT / 'arch/x86/kernel/drivers/video/framebuffer/scripts'
    sys.path.insert(0, str(scripts))
    spec = importlib.util.spec_from_file_location('shell_framebuffer', scripts / 'firmware.py')
    framebuffer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(framebuffer)
    spec = importlib.util.spec_from_file_location('shell_font', scripts / 'font/convert.py')
    font = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(font)
    raster = font.raster()
    base, stride, pixel_format, width, height = [number('KERNEL_FB_' + name) for name in
        ['BASE', 'STRIDE', 'FORMAT', 'WIDTH', 'HEIGHT']]
    if pixel_format not in (0, 1) or stride < width or not font.WIDTH <= width <= 1920 or not font.HEIGHT <= height <= 1080:
        raise RuntimeError('Invalid shell video geometry')
    columns, rows = width // font.WIDTH, height // font.HEIGHT
    cells, caret = expected(log, columns, rows)
    if 0xe8 not in cells or 0x20ac not in cells:
        raise RuntimeError('Italian glyph evidence missing from expected transcript')
    selected = {0, len(cells) - 1, cells.index(0xe8), cells.index(0x20ac)}
    prompts = [index for index, scalar in enumerate(cells) if scalar == ord('>')]
    selected.add(prompts[-1]); selected.add(caret)
    for index in sorted(selected):
        x, y = index % columns * font.WIDTH, index // columns * font.HEIGHT
        for row, bitmap in enumerate(raster.get(cells[index], raster[65533])):
            words = framebuffer.physical(vm, scenario / 'monitor.log', base + ((y + row) * stride + x) * 4, (font.WIDTH + 1) // 2)
            if index == caret and row >= font.HEIGHT - 2:
                bitmap = (1 << (4*font.WIDTH)) - 1
            for col in range(font.WIDTH):
                r, g, b = font.color(bitmap, col)
                color = r | g << 8 | b << 16 if pixel_format == 0 else b | g << 8 | r << 16
                if words[col // 2] >> ((col % 2) * 32) & 0xffffffff != color:
                    raise RuntimeError(f'Shell physical pixel mismatch: cell {index}, pixel {col},{row}')
    image = scenario / 'shell-screen.ppm'
    vm.stdin.write('screendump "' + image.as_posix() + '"\n'); vm.stdin.flush()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        data = image.read_bytes() if image.exists() else b''
        header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
        if header and len(data) - header.end() == width * height * 3:
            break
        if vm.poll() is not None:
            raise RuntimeError('Owned VM exited before shell screenshot')
        time.sleep(0.05)
    else:
        raise TimeoutError('Incomplete shell screenshot')
    if (int(header[1]), int(header[2])) != (width, height):
        raise RuntimeError('Invalid shell screenshot geometry')
    pixels = data[header.end():]
    for index, scalar in enumerate(cells):
        x, y = index % columns * font.WIDTH, index // columns * font.HEIGHT
        for row, bitmap in enumerate(raster.get(scalar, raster[65533])):
            if index == caret and row >= font.HEIGHT - 2:
                bitmap = (1 << (4*font.WIDTH)) - 1
            for col in range(font.WIDTH):
                color = bytes(font.color(bitmap, col))
                offset = ((y + row) * width + x + col) * 3
                if pixels[offset:offset + 3] != color:
                    raise RuntimeError(f'Shell visible pixel mismatch: cell {index}, pixel {col},{row}')
    return {'Cells': cells_done, 'Groups': groups, 'MaxPm': maximum, 'Overruns': overruns,
        'CaretCell': caret, 'PhysicalPixels': len(selected) * font.WIDTH * font.HEIGHT, 'VisiblePixels': len(cells) * font.WIDTH * font.HEIGHT}
