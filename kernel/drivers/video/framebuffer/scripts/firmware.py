"""Owned GOP EFI probe: verify mode readback and QEMU screenshot geometry."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[5]

# ============ hashes and serial metadata ============

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def read_serial(path):
    text = path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''
    return re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)


def number(log, name):
    match = re.search(name + r': 0x([0-9A-Fa-f]{16})', log)
    if not match:
        raise RuntimeError('Missing ' + name)
    return int(match.group(1), 16)

# ============ one owned VM ============

def boot(args, out, index):
    shutdown_timeout = getattr(args, 'shutdown_timeout', 5)
    if not 1 <= shutdown_timeout <= 30:
        raise ValueError('shutdown timeout must be 1..30 seconds')
    run = out / f'boot-{index}'
    esp = run / 'esp/EFI/BOOT'
    esp.mkdir(parents=True)
    shutil.copy2(args.image, esp / 'BOOTX64.EFI')
    if digest(args.image) != digest(esp / 'BOOTX64.EFI'):
        raise RuntimeError('ESP image hash mismatch')
    if args.kernel:
        shutil.copy2(args.kernel,esp / 'kernel.elf')
        (esp / 'boot-check.txt').write_bytes(b'OS boot check\r\n')
        if digest(args.kernel) != digest(esp / 'kernel.elf'):
            raise RuntimeError('ESP kernel hash mismatch')
    shutil.copy2(args.qemu / 'share/edk2-i386-vars.fd', run / 'vars.fd')
    serial, trace = run / 'serial.log', run / 'qemu.log'
    command = [str(args.qemu / 'qemu-system-x86_64.exe'), '-machine', 'q35',
               '-cpu', 'max', '-accel', 'tcg', '-m', '128', '-smp', '1',
               '-net', 'none', '-display', 'none', '-monitor', 'stdio',
               '-no-reboot', '-serial', 'file:' + serial.as_posix(),
               '-d', 'int,cpu_reset', '-D', trace.as_posix(),
               '-drive', 'if=pflash,format=raw,readonly=on,file=' +
               (args.qemu / 'share/edk2-x86_64-code.fd').as_posix(),
               '-drive', 'if=pflash,format=raw,file=' + (run / 'vars.fd').as_posix(),
               '-drive', 'format=raw,file=fat:rw:' + (run / 'esp').as_posix()]
    if args.absent:
        command += ['-vga', 'none']
    monitor_path, stderr_path = run / 'monitor.log', run / 'stderr.log'
    monitor_file = monitor_path.open('wb')
    stderr_file = stderr_path.open('wb')
    try:
        vm = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=monitor_file,
                              stderr=stderr_file, text=True,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    except BaseException:
        monitor_file.close()
        stderr_file.close()
        raise
    completed = False
    try:
        deadline = time.monotonic() + 40
        marker = 'KERNEL_READY' if args.kernel else ('GOP_UNAVAILABLE' if args.absent else 'GOP_SELECTION_READY')
        while time.monotonic() < deadline:
            log = read_serial(serial)
            if 'GOP_PROBE_FAILED' in log or re.search(r'\[\s*FAIL\s*\]', log):
                raise RuntimeError('Boot failure: ' + str(serial))
            if marker in log:
                break
            if vm.poll() is not None:
                raise RuntimeError('Owned VM exited before marker')
            time.sleep(0.05)
        else:
            raise TimeoutError('Missing ' + marker)
        record = {'Marker': marker}
        if args.kernel:
            expected = 'ABSENT' if args.absent else 'PRESENT'
            if 'KERNEL_FRAMEBUFFER_' + expected not in log or 'FRAMEBUFFER_HANDOFF_' + expected not in log:
                raise RuntimeError('Framebuffer presence contract disagrees with profile')
            if number(log,'KERNEL_BOOT_VERSION') != 4 or number(log,'KERNEL_BOOT_SIZE') != 208:
                raise RuntimeError('Kernel did not receive v4 ABI')
            record['Version'], record['Size'] = 4, 208
            if args.mapping_plan:
                if 'KERNEL_FRAMEBUFFER_PLAN_OK' not in log:
                    raise RuntimeError('Missing framebuffer preflight')
                pages = number(log,'KERNEL_FB_PLAN_PAGES')
                tables = number(log,'KERNEL_FB_PLAN_TABLES')
                if args.absent:
                    expected_pages, expected_tables = 0, 0
                else:
                    base, used = number(log,'KERNEL_FB_BASE'), number(log,'KERNEL_FB_USED')
                    expected_pages = ((base % 4096) + used - 1) // 4096 + 1
                    expected_tables = (expected_pages + 2 + 511) // 512 + 2
                if (pages,tables) != (expected_pages,expected_tables):
                    raise RuntimeError('Framebuffer plan disagrees with host calculation')
                record['PlanPages'], record['PlanTables'] = pages, tables
            if args.absent and any(number(log,'KERNEL_FB_' + key) for key in
                ['WIDTH','HEIGHT','STRIDE','FORMAT','BASE','CAPACITY','USED']):
                raise RuntimeError('Absent framebuffer metadata is not zero')
        vm.stdin.write('stop\ninfo registers\n')
        vm.stdin.flush()
        inspector = getattr(args, 'inspect_mapping', None)
        if inspector:
            record.update(inspector(vm, monitor_path, log, args))
        if not args.absent:
            if not args.kernel and 'GOP_PIXEL_VALIDATED' not in log:
                raise RuntimeError('Missing pixel validation evidence')
            record['PixelValidated'] = not bool(args.kernel)
            names = ['WIDTH', 'HEIGHT', 'STRIDE', 'FORMAT', 'BASE', 'CAPACITY', 'USED']
            if not args.kernel:
                names += ['QUERIES','RELEASED']
            prefix = 'KERNEL_FB_' if args.kernel else 'GOP_'
            record.update({name: number(log, prefix + name) for name in names})
            if not (0 < record['WIDTH'] <= 1920 and 0 < record['HEIGHT'] <= 1080 and
                    record['WIDTH'] <= record['STRIDE'] <= 4096 and record['FORMAT'] in (0, 1) and
                    record['USED'] == record['STRIDE'] * record['HEIGHT'] * 4 and
                    record['USED'] <= record['CAPACITY'] <= 32 * 1024 * 1024 and
                    record['BASE'] > 0 and record['BASE'] % 4 == 0 and
                    record['BASE'] + record['CAPACITY'] <= 1 << 52 and
                    (args.kernel or 0 < record['QUERIES'] == record['RELEASED'] <= 256)):
                raise RuntimeError('Invalid numeric mode/accounting evidence')
            screenshot = run / 'screen.ppm'
            vm.stdin.write('screendump ' + screenshot.as_posix() + '\n')
            vm.stdin.flush()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if screenshot.exists():
                    data = screenshot.read_bytes()
                    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
                    if header:
                        w, h = int(header[1]), int(header[2])
                        if len(data) >= header.end() + w * h * 3:
                            if (w, h) != (record['WIDTH'], record['HEIGHT']):
                                raise RuntimeError('Screenshot disagrees with GOP readback')
                            record['Screenshot'] = digest(screenshot)
                            break
                time.sleep(0.05)
            else:
                raise TimeoutError('Incomplete screenshot')
        vm.stdin.write('quit\n')
        vm.stdin.flush()
        vm.communicate(timeout=shutdown_timeout)
        stdout = monitor_path.read_text(encoding='utf-8', errors='replace')
        if vm.returncode != 0 or not re.search(r'RIP=[0-9a-fA-F]{16}', stdout):
            raise RuntimeError('Invalid exit/CPU capture')
        if trace.exists() and 'triple fault' in trace.read_text(errors='replace').lower():
            raise RuntimeError('Unexpected triple fault')
        completed = True
        return record
    finally:
        if vm.poll() is None:
            vm.kill()
            vm.communicate(timeout=5)
        monitor_file.close()
        stderr_file.close()
        if not completed:
            (run / 'failed.txt').write_text('Incomplete verification', encoding='utf-8')

# ============ consecutive positive boots or explicit absence ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--boots', type=int, default=10)
    parser.add_argument('--absent', action='store_true')
    parser.add_argument('--mapping-plan', action='store_true')
    parser.add_argument('--qemu', type=Path, default=Path('C:/Program Files/qemu'))
    args = parser.parse_args()
    if not 1 <= args.boots <= 20:
        parser.error('boots must be 1..20')
    if args.mapping_plan and not args.kernel:
        parser.error('mapping-plan requires kernel')
    out = ROOT / ('kernel/build/framebuffer-firmware-' + uuid.uuid4().hex)
    out.mkdir()
    production = ROOT / 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    original_hash = digest(production)
    evidence = {'Image': digest(args.image), 'OVMF': digest(args.qemu / 'share/edk2-x86_64-code.fd'),
                'Qemu': digest(args.qemu / 'qemu-system-x86_64.exe'),
                'ProductionLoader': original_hash, 'Boots': args.boots, 'Absent': args.absent,
                'MappingPlan': args.mapping_plan, 'Results': []}
    if args.kernel:
        evidence['Kernel'] = digest(args.kernel)
    try:
        for index in range(1, args.boots + 1):
            evidence['Results'].append(boot(args, out, index))
            print(f'PASS: GOP QEMU {index}/{args.boots}', flush=True)
    except BaseException as error:
        evidence['Failure'] = {'Boot': len(evidence['Results']) + 1,
                               'Type': type(error).__name__, 'Message': str(error)}
        raise
    finally:
        evidence['ProductionUnchanged'] = digest(production) == original_hash
        (out / 'results.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    if not evidence['ProductionUnchanged']:
        raise RuntimeError('Production loader changed')
    print('PASS: GOP firmware evidence ' + str(out))


if __name__ == '__main__':
    main()
