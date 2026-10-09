import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import uuid

ROOT = Path(__file__).resolve().parents[8]

# ============ owned boot provider ============

def provider():
    sys.dont_write_bytecode = True
    path = ROOT / 'kernel/drivers/video/framebuffer/scripts/firmware.py'
    spec = importlib.util.spec_from_file_location('owned_boot', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

# ============ consecutive boots per worker ============

def worker(api, args, out, absent, evidence):
    label = 'absent' if absent else 'video'
    folder = out / label
    folder.mkdir()
    profile = SimpleNamespace(image=args.image, kernel=args.kernel, qemu=args.qemu,
                              absent=absent, mapping_plan=True, shutdown_timeout=15)
    try:
        for index in range(1, args.boots + 1):
            record = api.boot(profile, folder, index)
            log = api.read_serial(folder / f'boot-{index}/serial.log')
            watchdog = {name: api.number(log, 'KERNEL_WATCHDOG_' + name)
                        for name in ('FAILURE', 'CLEANUP_FAILURE', 'FIRED', 'RESULT')}
            if (watchdog != {'FAILURE': 0, 'CLEANUP_FAILURE': 0, 'FIRED': 3, 'RESULT': 1} or
                'KERNEL_WATCHDOG_CLEANUP_OK' not in log):
                raise RuntimeError('Watchdog failed delivery or cleanup verification')
            record['Watchdog'] = watchdog
            evidence['Results'].append(record)
            print(f'PASS: concurrent {label} {index}/{args.boots}', flush=True)
    except BaseException as error:
        evidence['Failure'] = {'Boot': len(evidence['Results']) + 1,
                               'Type': type(error).__name__, 'Message': str(error)}
        raise

# ============ independent VM ownership and final manifest ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--boots', type=int, default=10)
    parser.add_argument('--qemu', type=Path, default=Path('C:/Program Files/qemu'))
    args = parser.parse_args()
    if not 1 <= args.boots <= 20:
        parser.error('boots must be 1..20 per worker')
    args.image = args.image.absolute()
    args.kernel = args.kernel.absolute()
    api = provider()
    out = ROOT / ('kernel/build/watchdog-arm-stress-' + uuid.uuid4().hex)
    out.mkdir()
    evidence = {'Image': api.digest(args.image), 'Kernel': api.digest(args.kernel),
                'OVMF': api.digest(args.qemu / 'share/edk2-x86_64-code.fd'),
                'Qemu': api.digest(args.qemu / 'qemu-system-x86_64.exe'),
                'Workers': 2, 'BootsPerWorker': args.boots,
                'Video': {'Results': []}, 'Absent': {'Results': []}}
    try:
        with ThreadPoolExecutor(max_workers=2) as executor:
            jobs = [executor.submit(worker, api, args, out, absent,
                                    evidence['Absent' if absent else 'Video'])
                    for absent in (False, True)]
            for job in as_completed(jobs):
                job.result()
    finally:
        evidence['ImagesUnchanged'] = (api.digest(args.image) == evidence['Image'] and
                                       api.digest(args.kernel) == evidence['Kernel'])
        (out / 'results.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    if not evidence['ImagesUnchanged']:
        raise RuntimeError('Input images changed during test')
    print('PASS: concurrent watchdog evidence ' + str(out), flush=True)


if __name__ == '__main__':
    main()
