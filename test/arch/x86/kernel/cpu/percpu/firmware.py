"""K3-A/B: BSP GS, retired early context and owned PM wait; APs remain stopped."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[6]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)
ap_spec = importlib.util.spec_from_file_location("ap_monitor", Path(__file__).parent / "resources/monitor.py")
ap_monitor = importlib.util.module_from_spec(ap_spec)
ap_spec.loader.exec_module(ap_monitor)
prep_spec = importlib.util.spec_from_file_location("ap_preparation", Path(__file__).parent / "preparation/monitor.py")
ap_preparation = importlib.util.module_from_spec(prep_spec)
prep_spec.loader.exec_module(ap_preparation)


# ====== monitor_read ======

def monitor_read(vm, command, ready):
    offset = vm.monitor.stat().st_size
    vm.command(command)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        text = vm.monitor.read_bytes()[offset:].decode("utf-8", errors="replace")
        result = ready(text)
        if result is not None:
            return result
        if not vm.alive():
            raise RuntimeError("Owned VM exited during monitor read")
        time.sleep(0.005)
    raise TimeoutError("Incomplete monitor response: " + command)


# ====== gs_base ======

def gs_base(text):
    match = re.search(r"(?m)^GS\s*=\s*[0-9a-fA-F]{4}\s+([0-9a-fA-F]{16})", text)
    return int(match[1], 16) if match else None


# ====== memory ======

def memory(vm, address, count):
    def collect(text):
        values = {}
        for match in re.finditer(r"(?m)^([0-9a-fA-F]{8,16}):\s*((?:0x[0-9a-fA-F]{16}\s*)+)", text):
            start = int(match[1], 16)
            for index, value in enumerate(re.findall(r"0x([0-9a-fA-F]{16})", match[2])):
                values[start + index * 8] = int(value, 16)
        return [values[address + index * 8] for index in range(count)] if all(
            address + index * 8 in values for index in range(count)) else None
    return monitor_read(vm, f"x /{count}gx 0x{address:x}", collect)


# ====== identity ======

def identity(vm, log, configured, context):
    if log.count("KERNEL_CPU_IDENTITIES_READY", "OK") != 1:
        raise RuntimeError("Missing or duplicate identity publication")
    address = log.number("KERNEL_CPU_IDENTITIES")
    if address & 7 or not 0xffffffff80000000 <= address <= 0xffffffff80400000 - 1056:
        raise RuntimeError("Identity owner outside kernel image")
    owner = memory(vm, address, 132)
    count, offline, ignored = (log.number("KERNEL_CPU_" + name) for name in ("ENABLED", "OFFLINE", "IGNORED"))
    if count != configured or owner[:4] != [address, context, count | (offline << 32), ignored | (2 << 32)]:
        raise RuntimeError("Identity header disagrees with owner/context/configuration")
    madt = log.number("KERNEL_MADT_TOPOLOGY")
    if madt & 7 or madt < 0xffff800000000000:
        raise RuntimeError("MADT pointer outside kernel half")
    source = memory(vm, madt, 8)
    total = source[4] & 0xffffffff
    if not 1 <= total <= 64 or total != log.number("KERNEL_MADT_CPU_COUNT"):
        raise RuntimeError("MADT count mismatch")
    raw = memory(vm, madt + 64, total * 2)
    rows = [(raw[i * 2] & 0xffffffff, raw[i * 2] >> 32,
             raw[i * 2 + 1] & 0xffffffff, raw[i * 2 + 1] >> 32) for i in range(total)]
    enabled = [r for r in rows if r[2] == 1]
    bsp_id, bsp_uid = log.number("KERNEL_APIC_INITIAL_ID"), log.number("KERNEL_APIC_BSP_UID")
    bsp = [r for r in enabled if r[0] == bsp_uid and r[1] == bsp_id]
    if len(bsp) != 1 or count != len(enabled) or offline != sum(r[2] == 2 for r in rows) or ignored != sum(r[2] == 0 for r in rows):
        raise RuntimeError("Identity classification or BSP mismatch")
    expected = bsp + [r for r in enabled if r != bsp[0]]
    for index, (uid, apic, flags, kind) in enumerate(expected):
        if owner[4 + index * 2:6 + index * 2] != [index | (apic << 32), uid | (kind << 32)]:
            raise RuntimeError("Logical CPU mapping differs from actual MADT")
    if any(owner[4 + count * 2:]):
        raise RuntimeError("Nonzero unused identity entries")
    return dict(Enabled=count, Offline=offline, Ignored=ignored, BspId=bsp_id, BspUid=bsp_uid)


# ====== boot ======

def boot(image, out, index, ordinary, committed, smp):
    configured = smp or (1 if index % 2 else 4)
    with qemu.Vm(out / f"boot-{index}", image, "std", smp=configured,
                 memory=128 if index % 2 else 256) as vm:
        log = vm.wait("KERNEL_READY", 90)
        if log.failures():
            raise RuntimeError("Unexpected failure: " + str(log.failures()))
        for marker in ("KERNEL_LOCK_CONTEXT_READY", "KERNEL_PERCPU_BSP_READY", "KERNEL_PERCPU_EARLY_RETIRED",
                       "KERNEL_HEAP_SYNC_OK", "KERNEL_PERCPU_WAIT_READY"):
            if log.count(marker, "OK") != 1:
                raise RuntimeError("Missing or duplicate " + marker)
        if log.count("KERNEL_LOWER_HALF_EMPTY", "OK") != 2:
            raise RuntimeError("Missing retirement or post-heap lower-half check")
        if not log.before("KERNEL_LOCK_CONTEXT_READY", "KERNEL_PERCPU_BSP_READY"):
            raise RuntimeError("Promotion before early context")
        if not ordinary and not log.has("KERNEL_SYNC_PROBE_OK", "OK"):
            raise RuntimeError("Real IF/guard diagnostic did not complete")
        if ordinary and "KERNEL_SYNC_" in log.text:
            raise RuntimeError("Test sync code in ordinary image")
        base, context, early = (log.number("KERNEL_PERCPU_" + name) for name in ("BASE", "CONTEXT", "EARLY"))
        if base & 63 or context != base + 24 or log.number("KERNEL_PERCPU_INDEX"):
            raise RuntimeError("Per-CPU software layout")
        if not all(0xffffffff80000000 <= value < 0xffffffff80400000 for value in (base, context, early)):
            raise RuntimeError("Per-CPU state outside kernel image window")
        vm.command("stop")
        actual = monitor_read(vm, "info registers", gs_base)
        resources = ap_monitor.verify(vm, log, memory, configured, ordinary)
        preparation = ap_preparation.verify(vm, log, memory, configured, ordinary, committed)
        identities = identity(vm, log, configured, context)
        owner = memory(vm, base, 56)
        retired = memory(vm, early, 52)
        if actual != base or owner[0] != base or owner[1] != early or owner[2] != 2 << 32 or owner[3] != context:
            raise RuntimeError("GS register or actual owner disagrees with serial")
        wait, source = (log.number("KERNEL_PERCPU_WAIT" + suffix) for suffix in ("", "_SOURCE"))
        if not all(0xffffffff80000000 <= value < 0xffffffff80400000 for value in (wait, source)):
            raise RuntimeError("Wait/source outside kernel image")
        descriptor, pm = memory(vm, wait, 4), memory(vm, source, 3)
        mask, port = log.number("KERNEL_PERCPU_WAIT_MASK"), log.number("KERNEL_PERCPU_WAIT_PORT")
        ticks, polls = log.number("KERNEL_PERCPU_WAIT_TICKS"), log.number("KERNEL_PERCPU_WAIT_POLLS")
        if (source != wait + 32 or descriptor[1] != source or descriptor[2] != (3579545 << 32) | mask or
                descriptor[3] != (polls << 32) | ticks or ticks != 357955 or polls != 1000000 or
                mask not in (0xffffff, 0xffffffff) or not 0x100 <= port <= 0xfffc or port & 3 or
                not 0xffffffff80000000 <= descriptor[0] < 0xffffffff80400000 or
                pm[0] < 4096 or not 116 <= (pm[1] & 0xffffffff) <= 4096 or
                pm[1] >> 32 != port or pm[2] != (1 << 32) | mask):
            raise RuntimeError("Actual PM wait descriptor/source mismatch")
        if owner[4] <= 1 or owner[5] != wait or owner[6] or any(owner[7:]) or any(retired):
            raise RuntimeError("Context not quiescent, early context not retired or tokens reset")
        return dict(ConfiguredVcpus=configured, OsContextsReady=1, GSBase=hex(actual),
                    Context=hex(context), Retired=hex(early), NextToken=owner[4], Ordinary=ordinary, Wait=hex(wait), PmPort=port, PmMask=hex(mask), Identities=identities, ApResources=resources, ApPreparation=preparation)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--ordinary", action="store_true")
    parser.add_argument("--committed", action="store_true")
    parser.add_argument("--smp", type=int, choices=(1, 4))
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("percpu-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(1, args.boots + 1):
            results.append(boot(args.image, out, index, args.ordinary, args.committed, args.smp))
            print(f"PASS: BSP GS {index}/{args.boots}; monitor base, retired context, usable locks; APs not started", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "percpu", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "percpu", args.image, results, len(results), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
