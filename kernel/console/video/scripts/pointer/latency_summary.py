"""V2d M2-A summary: GOP and V2c interactive campaigns side by side (medians and maxima per boot).

Usage: python -B latency_summary.py <gop evidence dir> <v2c evidence dir>
Values are ACPI PM timer ticks (3.579545 MHz) of the QEMU virtual clock: comparable between the
two profiles, not absolute hardware latencies. DrainSeconds is host wall time of the emulation.
"""
import json
import re
import statistics
import sys
from pathlib import Path

PM_HZ = 3579545

# ====== numbers ======

def number(log, name):
    found = re.findall(re.escape(name) + r': 0x([0-9a-fA-F]+)', log)
    return int(found[-1], 16) if found else None

def boots(directory):
    for serial in sorted(Path(directory).glob('boot-*/serial.log')):
        yield re.sub(r'\x1b\[[0-9;]*m', '', serial.read_text(errors='replace')).replace('\r', '')

# ====== metrics ======

def metrics(log):
    flushes = number(log, 'KERNEL_CONSOLE_FLUSHES') or 0
    row = dict(Flushes=flushes, FlushMeanPm=(number(log, 'KERNEL_CONSOLE_FLUSH_TOTAL_PM') or 0) / max(flushes, 1),
        FlushMaxPm=number(log, 'KERNEL_CONSOLE_FLUSH_MAX_PM') or 0)
    for key, name in (('Present', 'KERNEL_VGPU_PRESENT_PM'), ('FlushWait', 'KERNEL_VGPU_FLUSH_WAIT_PM'),
            ('Poll', 'KERNEL_VGPU_POLL_PM'), ('Quiet', 'KERNEL_VGPU_QUIET_PM'), ('Area', 'KERNEL_VGPU_PRESENT_AREA')):
        count = number(log, name + '_COUNT')
        if count is None: continue
        row[key + 'Count'] = count
        row[key + 'Mean'] = (number(log, name + '_TOTAL') or 0) / max(count, 1)
        row[key + 'Max'] = number(log, name + '_MAX') or 0
    return row

def drain(directory):
    results = json.loads((Path(directory) / 'results.json').read_text())
    return [value for record in results['Results'] for value in record.get('DrainSeconds', [])]

# ====== main ======

def main(gop, v2c):
    table = {}
    for label, directory in (('GOP', gop), ('V2c', v2c)):
        rows = [metrics(log) for log in boots(directory)]
        keys = sorted({key for row in rows for key in row})
        table[label] = {key: dict(Median=statistics.median(row[key] for row in rows if key in row),
            Max=max(row[key] for row in rows if key in row)) for key in keys}
        seconds = drain(directory)
        table[label]['DrainSeconds'] = dict(Median=statistics.median(seconds), Max=max(seconds))
        table[label]['Boots'] = len(rows)
    print(json.dumps(table, indent=2))
    for label in table:
        for key in ('FlushMeanPm', 'PresentMean', 'PresentMax', 'FlushWaitMean', 'PollMean', 'QuietMax'):
            if key in table[label]:
                value = table[label][key]['Median']
                print(f'{label:4s} {key:14s} median {value:12.1f} ticks = {value * 1e6 / PM_HZ:10.1f} us')

if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
