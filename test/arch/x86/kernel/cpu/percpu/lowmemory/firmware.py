"""K3-G: inspect the early pinned lease, source map and absence of low aliases."""
import argparse
import importlib.util
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[7]
spec = importlib.util.spec_from_file_location("percpu", Path(__file__).parents[1] / "firmware.py")
cpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpu)
qemu = cpu.qemu


# ====== verify ======

def verify(vm, log, absent, expected_failures=()):
    if log.failures() != list(expected_failures) or not log.before("KERNEL_LOCK_CONTEXT_READY", "KERNEL_PHYSICAL_OK"):
        raise RuntimeError("Boot failure or missing early lock context")
    marker = "KERNEL_AP_LOW_UNAVAILABLE" if absent else "KERNEL_AP_LOW_RESERVED"
    if log.count(marker, "WARN" if absent else "OK") != 1 or not log.before(marker, "KERNEL_PHYSICAL_OK"):
        raise RuntimeError("Reservation not published before generic allocations")
    address, pool, base, ident = (log.number("KERNEL_AP_LOW_" + name) for name in ("OWNER", "POOL", "BASE", "ID"))
    if any(not 0xffffffff80000000 <= value < 0xffffffff80400000 for value in (address, pool)):
        raise RuntimeError("Owners outside static kernel image")
    owner = cpu.memory(vm, address, 7)
    if owner != [address, pool, base, 0 if absent else 1, ident, 0 if absent else pool, 1 if absent else 2]:
        raise RuntimeError("Actual owner differs from publication")
    header = cpu.memory(vm, pool, 6)
    free_count = header[5] & 0xffffffff
    if header[0] != pool or not 0 < free_count <= 256:
        raise RuntimeError("Allocator header corrupt")
    live = cpu.memory(vm, pool + 4144, 256)
    pins = cpu.memory(vm, pool + 6192, 32)
    free = cpu.memory(vm, pool + 48, free_count * 2)
    low = cpu.memory(vm, cpu.ap_monitor.recursive(510, 510, 510, 510), 256)
    if any(low):
        raise RuntimeError("Low mapping appeared in operational CR3")
    if absent:
        if base or ident:
            raise RuntimeError("Absent diagnostic retained an early lease")
        return dict(Available=False, Injected=True, LowHalfEmpty=True)
    if not 0x1000 <= base <= 0xff000 or base & 4095 or ident != 1:
        raise RuntimeError("Reservation is not the first allocation or outside SIPI range")
    matches = [i for i in range(64) if live[i * 4:i * 4 + 4] == [base, 1, 1, pool]]
    if len(matches) != 1 or (pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1:
        raise RuntimeError("Actual lease not unique and pinned")
    for i in range(free_count):
        start, pages = free[i * 2:i * 2 + 2]
        if start <= base < start + pages * 4096:
            raise RuntimeError("Reserved page remains allocatable")
    context = cpu.memory(vm, log.number("KERNEL_ARENA_CONTEXT"), 13)
    mapped, size, stride = context[9], context[10], context[12]
    if not 40 <= stride <= 4096 or size % stride or not 0 < size <= 1024 * 1024:
        raise RuntimeError("Source descriptor dimensions")
    words = cpu.memory(vm, mapped, (size + 7) // 8)
    raw = b"".join(w.to_bytes(8, "little") for w in words)
    covered = []
    for offset in range(0, size, stride):
        row = raw[offset:offset + stride]
        kind = int.from_bytes(row[:4], "little")
        start = int.from_bytes(row[8:16], "little")
        pages = int.from_bytes(row[24:32], "little")
        attrs = int.from_bytes(row[32:40], "little")
        if start <= base and base + 4096 <= start + pages * 4096:
            covered.append((kind, attrs))
    if len(covered) != 1 or covered[0][0] != 7 or not covered[0][1] & 8:
        raise RuntimeError("Lease not backed by conventional WB source memory")
    return dict(Available=True, Base=hex(base), Lease=1, Pinned=True, LowHalfEmpty=True)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--absent", action="store_true")
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("ap-low-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    rows = []
    try:
        for index in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{index}", args.image, "std", smp=1 if index % 2 else 4) as vm:
                log = vm.wait("KERNEL_READY", 90)
                rows.append(verify(vm, log, args.absent))
                print(f"PASS: early reservation {index}/{args.boots}; actual lease/map/pin/CR3", flush=True)
    except Exception as failure:
        rows.append(dict(Failure=str(failure)))
        qemu.finish(out, "ap-low", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-low", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
