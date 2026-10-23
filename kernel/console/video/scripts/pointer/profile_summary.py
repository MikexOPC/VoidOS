"""Flush profile summary (P2-A): where the ticks of a console flush go in images A, B and C.

Usage: python -B profile_summary.py <A evidence> <B evidence> <C evidence>
A: GOP console. B: translated GPU domain and surface, console on GOP. C: full V2c.
Per image: median over boots of the mean ticks per flush spent in each phase. PM ticks of the
QEMU TCG virtual clock (3.579545 MHz): comparable between images, not hardware latencies.
"""
import re
import statistics
import sys
from pathlib import Path

PHASES = ('FLUSH', 'RENDER', 'GROUP', 'VERIFY', 'TARGET', 'PREPARE', 'PRESENT', 'POLL')

# ====== numbers ======

def number(log, name):
    found = re.findall(re.escape(name) + r': 0x([0-9a-fA-F]+)', log)
    return int(found[-1], 16) if found else 0

def per_flush(log):
    flushes = max(number(log, 'KERNEL_PROFILE_FLUSH_COUNT'), 1)
    row = {phase: number(log, f'KERNEL_PROFILE_{phase}_TOTAL') / flushes for phase in PHASES}
    row['VERIFY_EACH'] = number(log, 'KERNEL_PROFILE_VERIFY_TOTAL') / max(number(log, 'KERNEL_PROFILE_VERIFY_COUNT'), 1)
    # Time in the flush outside render, present and poll; and inside a group outside the timed parts.
    row['FLUSH_OTHER'] = row['FLUSH'] - row['RENDER'] - row['PRESENT'] - row['POLL']
    row['GROUP_OTHER'] = row['GROUP'] - row['VERIFY'] - row['TARGET'] - row['PREPARE']
    row['RENDER_OUTSIDE_GROUP'] = row['RENDER'] - row['GROUP']
    return row

def image(directory):
    rows = []
    for serial in sorted(Path(directory).glob('boot-*/serial.log')):
        log = re.sub(r'\x1b\[[0-9;]*m', '', serial.read_text(errors='replace')).replace('\r', '')
        rows.append(per_flush(log))
    return {key: statistics.median(row[key] for row in rows) for key in rows[0]}, len(rows)

# ====== main ======

def main(paths):
    images = [image(path) for path in paths]
    keys = list(PHASES) + ['VERIFY_EACH', 'FLUSH_OTHER', 'GROUP_OTHER', 'RENDER_OUTSIDE_GROUP']
    print(f"{'ticks per flush':22s}" + ''.join(f'{label:>12s}' for label in ('A', 'B', 'C')))
    for key in keys:
        print(f'{key:22s}' + ''.join(f'{values[key]:12.0f}' for values, _ in images))
    print('boots' + ' ' * 17 + ''.join(f'{count:12d}' for _, count in images))

if __name__ == '__main__':
    if len(sys.argv) != 4: raise SystemExit(__doc__)
    main(sys.argv[1:])
