import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
import uuid
sys.dont_write_bytecode = True
import drawing
ROOT = Path(__file__).resolve().parents[7]
spec = importlib.util.spec_from_file_location('gop_probe',
    ROOT / 'kernel/drivers/video/framebuffer/scripts/firmware.py')
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
ADDRESS = 0x000ffffffffff000
NX = 1 << 63

# ============ independent pixel coordinates ============

def pixel_cases(width, height):
    return [(0, 0, (255, 0, 0)), (width - 1, 0, (0, 255, 0)),
        (0, height - 1, (0, 0, 255)), (width - 1, height - 1, (255, 255, 255)),
        (width // 2, height // 2, (17, 34, 51))]

# ============ PPM visible-color verification ============

def screenshot_pixels(path, record, draw, console=False):
    data = path.read_bytes()
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not header or (int(header[1]), int(header[2])) != (record['WIDTH'], record['HEIGHT']):
        raise RuntimeError('Invalid screenshot geometry')
    image = data[header.end():]
    for x, y, color in pixel_cases(record['WIDTH'], record['HEIGHT']):
        offset = (y * record['WIDTH'] + x) * 3
        if image[offset:offset + 3] != bytes(color):
            raise RuntimeError(f'Visible pixel mismatch at {x},{y}')
    return 5 + (drawing.verify_visible(image, record['WIDTH'], console) if draw else 0)

# ============ bounded independent physical reads ============

def physical(vm, monitor, base, count):
    vm.stdin.write(f'xp /{count}gx 0x{base:x}\n')
    vm.stdin.flush()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        text = monitor.read_text(errors='replace')
        values = {}
        for match in re.finditer(r'(?m)^([0-9a-fA-F]{8,16}):\s*((?:0x[0-9a-fA-F]{16}\s*)+)', text):
            start = int(match[1], 16)
            for index, value in enumerate(re.findall(r'0x([0-9a-fA-F]{16})', match[2])):
                values[start + index * 8] = int(value, 16)
        if all(base + index * 8 in values for index in range(count)):
            return [values[base + index * 8] for index in range(count)]
        if vm.poll() is not None:
            raise RuntimeError('Owned VM exited during page-table inspection')
        time.sleep(0.05)
    raise TimeoutError(f'Incomplete physical read at {base:x}')

# ============ validate published branch guards and every leaf ============

def inspect(vm, monitor, log, args):
    if args.absent:
        if 'KERNEL_FB_MAPPING_ABSENT' not in log or 'KERNEL_FB_MAPPING_LIVE' in log:
            raise RuntimeError('Absent display allocated a live mapping')
        if 'KERNEL_FB_PIXELS_OK' in log:
            raise RuntimeError('Pixels attempted without display')
        if 'KERNEL_FB_DRAWING_OK' in log:
            raise RuntimeError('Drawing attempted without display')
        return {'LiveMapping': False}
    if 'KERNEL_FB_MAPPING_LIVE' not in log:
        raise RuntimeError('Missing live mapping marker')
    if args.rollback and 'KERNEL_FB_ROLLBACK_OK' not in log:
        raise RuntimeError('Missing real unbind budget restoration')
    n = lambda name: provider.number(log, 'KERNEL_FB_' + name)
    root = n('ROOT')
    pages = ((n('BASE') & 4095) + n('USED') + 4095) // 4096
    pt_count = (pages + 2 + 511) // 512
    cache, pat = n('CACHE'), n('PAT')
    index = ((cache >> 7) & 1) * 4 + ((cache >> 4) & 1) * 2 + ((cache >> 3) & 1)
    if cache & ~0x98 or (pat >> (index * 8)) & 255 or pat & 255 != 6:
        raise RuntimeError('Video cache is not strong UC with RAM PA0 WB')
    if n('VIRTUAL') != (225 << 39) + 4096 or n('TABLE_COUNT') != pt_count + 2:
        raise RuntimeError('Unexpected geometry/branch')
    if n('BUDGET_BEFORE') - n('BUDGET_LIVE') != pt_count + 2:
        raise RuntimeError('Physical budget does not match retained owned tables')
    root_entry = physical(vm, monitor, root + 225 * 8, 1)[0]
    if root_entry & ~0x60 != n('TABLE0') | 3:
        raise RuntimeError('Live root does not point to owned PDPT')
    pdpt = physical(vm, monitor, n('TABLE0'), 512)
    if pdpt[0] & ~ADDRESS & ~0x60 != 3 or any(pdpt[1:]):
        raise RuntimeError('Invalid PDPT flags or extra mappings')
    directory = physical(vm, monitor, pdpt[0] & ADDRESS, 512)
    seen = {root, n('TABLE0'), pdpt[0] & ADDRESS}
    if len(seen) != 3 or any(directory[pt_count:]):
        raise RuntimeError('Aliased owned tables or unexpected PDE')
    present = 0
    for table in range(pt_count):
        entry = directory[table]
        base = entry & ADDRESS
        if entry & ~ADDRESS & ~0x60 != 3 or base in seen:
            raise RuntimeError('Unsafe PDE or duplicate table')
        seen.add(base)
        leaves = physical(vm, monitor, base, 512)
        for slot, leaf in enumerate(leaves):
            logical = table * 512 + slot
            expected = 0 if logical == 0 or logical > pages else (
                (n('BASE') & ~4095) + (logical - 1) * 4096) | NX | cache | 3
            if leaf & ~0x60 != expected:
                raise RuntimeError(f'Unsafe video leaf/guard {logical}: {leaf:x} != {expected:x}')
            present += bool(expected)
    text = monitor.read_text(errors='replace')
    registers = re.findall(r'CR3=([0-9a-fA-F]{16})', text)
    if not registers or int(registers[-1], 16) != root:
        raise RuntimeError('Actual CR3 changed')
    if args.pixels:
        if 'KERNEL_FB_PIXELS_OK' not in log:
            raise RuntimeError('Missing kernel pixel readback/bounds evidence')
        for x, y, color in pixel_cases(n('WIDTH'), n('HEIGHT')):
            address = n('BASE') + (y * n('STRIDE') + x) * 4
            value = (physical(vm, monitor, address & ~7, 1)[0] >> ((address & 7) * 8)) & 0xffffffff
            red, green, blue = color
            expected = (red | (green << 8) | (blue << 16)) if n('FORMAT') == 0 else (
                blue | (green << 8) | (red << 16))
            if value != expected:
                raise RuntimeError(f'Physical pixel mismatch at {x},{y}')
    raster = 0
    if args.drawing:
        if 'KERNEL_FB_DRAWING_OK' not in log:
            raise RuntimeError('Missing bounded drawing marker')
        raster = drawing.verify_physical(physical, vm, monitor, n('BASE'), n('STRIDE'), n('FORMAT'), args.console_render)
    elif not args.rollback and 'KERNEL_FB_DRAWING_OK' in log:
        raise RuntimeError('Drawing probe in ordinary image')
    return {'LiveMapping': True, 'VerifiedLeaves': present, 'OwnedTables': len(seen) - 1,
            'Rollback': args.rollback, 'CacheIndex': index, 'Root': root,
            'PhysicalPixels': (5 if args.pixels else 0) + raster, 'DrawingPixels': raster}

# ============ image-specific consecutive boots ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--boots', type=int, default=10)
    parser.add_argument('--absent', action='store_true')
    parser.add_argument('--rollback', action='store_true')
    parser.add_argument('--pixels', action='store_true')
    parser.add_argument('--drawing', action='store_true')
    parser.add_argument('--console-render', action='store_true')
    parser.add_argument('--shutdown-timeout', type=float, default=5)
    args = parser.parse_args()
    if not 1 <= args.boots <= 20 or not 1 <= args.shutdown_timeout <= 30 or (args.absent and args.rollback):
        parser.error('boots 1..20; rollback requires video')
    if args.pixels and (args.absent or not args.rollback):
        parser.error('pixels requires rollback diagnostic with video')
    if args.drawing and not args.pixels:
        parser.error('drawing requires pixels diagnostic')
    if args.console_render and not args.drawing:
        parser.error('console rendering requires drawing diagnostic')
    args.image = ROOT / 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    args.qemu = Path('C:/Program Files/qemu')
    args.mapping_plan = True
    args.inspect_mapping = inspect
    out = ROOT / ('kernel/build/framebuffer-live-' + uuid.uuid4().hex)
    out.mkdir()
    evidence = {'Kernel': provider.digest(args.kernel), 'Loader': provider.digest(args.image),
        'OVMF': provider.digest(args.qemu / 'share/edk2-x86_64-code.fd'),
        'Qemu': provider.digest(args.qemu / 'qemu-system-x86_64.exe'),
        'Absent': args.absent, 'Rollback': args.rollback, 'Pixels': args.pixels,
        'Drawing': args.drawing, 'Results': []}
    evidence['ConsoleRender'] = args.console_render
    evidence['ShutdownTimeoutSeconds'] = args.shutdown_timeout
    try:
        for index in range(1, args.boots + 1):
            record = provider.boot(args, out, index)
            if args.pixels:
                record['VisiblePixels'] = screenshot_pixels(out / f'boot-{index}/screen.ppm', record, args.drawing, args.console_render)
                record['PixelValidated'] = True
            evidence['Results'].append(record)
            print(f'PASS: framebuffer live {index}/{args.boots}', flush=True)
    except BaseException as error:
        evidence['Failure'] = str(error)
        raise
    finally:
        evidence['LoaderUnchanged'] = evidence['Loader'] == provider.digest(args.image)
        (out / 'results.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    if not evidence['LoaderUnchanged']:
        raise RuntimeError('Loader changed')
    print('PASS: evidence ' + str(out), flush=True)


if __name__ == '__main__':
    main()
