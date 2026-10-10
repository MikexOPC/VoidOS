import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[4]

# ============ active resource campaign verification ============

def verify(directory, digest, boots):
    evidence = json.loads((directory / 'results.json').read_text(encoding='utf-8'))
    if evidence.get('Failure') or evidence.get('Image') != digest or evidence.get('Boots') != boots or \
            len(evidence.get('Results', [])) != boots or evidence.get('ItalianText') is not True or \
            evidence.get('Shell') is not True or evidence.get('StressKeys') != 32:
        raise RuntimeError('Invalid active-resource campaign')
    for index, result in enumerate(evidence['Results'], 1):
        log = (directory / f'boot-{index}/serial.log').read_text(errors='replace')
        markers = ['KERNEL_FB_MAPPING_LIVE', 'KERNEL_PS2_TASK_POLL_READY',
            'KERNEL_PS2_INPUT_CLEANUP_OK', 'KERNEL_WATCHDOG_CLEANUP_OK',
            'KERNEL_CONSOLE_ACTIVE_RESOURCES_OK', 'KERNEL_READY']
        positions = [log.find(marker) for marker in markers]
        if -1 in positions or positions != sorted(positions) or \
                re.search(r'KERNEL_EXCEPTION_HALTED|\bPANIC\b|DISPATCH_FAILED|PS2_INPUT_FAILED', log):
            raise RuntimeError('Missing or out-of-order active resource markers')
        if not result.get('BudgetBefore') or result.get('BudgetBefore') != result.get('BudgetAfter') or \
                not result.get('PS2_TASK_IRQS') or result.get('PS2_IDLE', 0) < 2:
            raise RuntimeError('Insufficient IRQ, heartbeat or budget evidence')
    return evidence

# ============ owned PS2 provider and console-specific manifest ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--boots', type=int, default=10, choices=range(1, 21))
    args = parser.parse_args()
    image = args.kernel.resolve(strict=True)
    digest = hashlib.sha256(image.read_bytes()).hexdigest().upper()
    loader = ROOT / 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    loader_digest = hashlib.sha256(loader.read_bytes()).hexdigest().upper()
    provider = ROOT / 'arch/x86/kernel/drivers/input/ps2/scripts/firmware.py'
    result = subprocess.run([sys.executable, str(provider), '--image', str(image),
        '--boots', str(args.boots), '--text', '--shell', '--stress', '32'],
        capture_output=True, text=True, check=False)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        raise RuntimeError('Owned PS2 provider failed')
    match = re.search(r'PASS: PS/2 input firmware evidence (.+)', result.stdout)
    if not match:
        raise RuntimeError('Missing provider evidence')
    directory = Path(match[1].strip())
    evidence = verify(directory, digest, args.boots)
    if hashlib.sha256(image.read_bytes()).hexdigest().upper() != digest:
        raise RuntimeError('Image changed during campaign')
    if evidence.get('Loader') != loader_digest or \
            hashlib.sha256(loader.read_bytes()).hexdigest().upper() != loader_digest:
        raise RuntimeError('Loader changed during campaign')
    output = directory / 'console-active-results.json'
    output.write_text(json.dumps({'Image': digest, 'Boots': args.boots,
        'FramebufferLiveDuringInput': True, 'RenderingUnderLoad': False, 'LoaderUnchanged': True,
        'IRQContext': 'existing audited Assembly task window',
        'Provider': str(directory / 'results.json'), 'Results': evidence['Results']},
        indent=2), encoding='utf-8')
    print('PASS: console active resource evidence ' + str(output), flush=True)


if __name__ == '__main__':
    main()
