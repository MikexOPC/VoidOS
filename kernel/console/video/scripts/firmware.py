import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[4]

# ============ final image and console-specific evidence ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--boots', type=int, default=10)
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--absent', action='store_true')
    parser.add_argument('--shutdown-timeout', type=float, default=15)
    args = parser.parse_args()
    if not 1 <= args.boots <= 20 or not 1 <= args.shutdown_timeout <= 30 or (args.diagnostic and args.absent):
        parser.error('boots1..20; diagnostic requires video')
    image = args.kernel.resolve(strict=True)
    digest = hashlib.sha256(image.read_bytes()).hexdigest().upper()
    provider = ROOT / 'arch/x86/kernel/drivers/video/framebuffer/scripts/firmware.py'
    command = [sys.executable, str(provider), '--kernel', str(image), '--boots', str(args.boots),
        '--shutdown-timeout', str(args.shutdown_timeout)]
    if args.diagnostic:
        command += ['--rollback', '--pixels', '--drawing']
    if args.absent:
        command += ['--absent']
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        print(result.stderr, end='', file=sys.stderr)
        raise RuntimeError('Owned framebuffer firmware provider failed')
    match = re.search(r'PASS: evidence (.+)', result.stdout)
    if not match:
        raise RuntimeError('Missing firmware evidence path')
    directory = Path(match[1].strip())
    evidence = json.loads((directory / 'results.json').read_text(encoding='utf-8'))
    if evidence.get('Failure') or evidence['Kernel'] != digest or not evidence['LoaderUnchanged'] or \
            len(evidence['Results']) != args.boots:
        raise RuntimeError('Invalid final-image campaign')
    for boot in range(1, args.boots + 1):
        log = (directory / f'boot-{boot}/serial.log').read_text(errors='replace')
        present = 'KERNEL_CONSOLE_GRID_OK' in log
        if present != args.diagnostic:
            raise RuntimeError(f'Unexpected console probe state in boot{boot}')
    output = directory / 'console-grid-results.json'
    output.write_text(json.dumps({'Kernel': digest, 'Diagnostic': args.diagnostic,
        'Absent': args.absent, 'Boots': args.boots, 'GridProbePerBoot': args.diagnostic,
        'ShutdownTimeoutSeconds': args.shutdown_timeout,
        'Provider': str(directory / 'results.json')}, indent=2), encoding='utf-8')
    print('PASS: console-specific evidence ' + str(output), flush=True)


if __name__ == '__main__':
    main()
