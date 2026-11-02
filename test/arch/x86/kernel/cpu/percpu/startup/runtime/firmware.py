"""Ordinary BSP continuation with real APs parked; no scheduler SMP claim."""
import argparse
import importlib.util
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("runtime_launch", Path(__file__).parents[1] / "transport/firmware.py")
launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launch)
cpu, qemu, transition = launch.cpu, launch.qemu, launch.transition


# ====== allocator ======

def allocator(vm, log):
    pool = log.number("KERNEL_AP_LOW_POOL")
    header = cpu.memory(vm, pool, 6)
    live, pins = cpu.memory(vm, pool + 4144, 256), cpu.memory(vm, pool + 6192, 32)
    count = header[5] & 0xffffffff
    if header[0] != pool or not 0 < count <= 256:
        raise RuntimeError("Allocator header changed")
    free = cpu.memory(vm, pool + 48, count * 2)
    leases = [live[i:i + 4] for i in range(0, 256, 4) if live[i + 2]]
    if header[1] != header[2] + sum(r[1] for r in leases) or header[2] != sum(free[i] for i in range(1, len(free), 2)):
        raise RuntimeError("Physical total/free/live accounting differs")
    ranges = [(free[i], free[i] + free[i + 1] * 4096) for i in range(0, len(free), 2)]
    for base, pages, ident, owner in leases:
        if owner != pool or not pages or base & 4095 or ident >= header[3]:
            raise RuntimeError("Invalid live physical lease")
        ranges.append((base, base + pages * 4096))
    ranges.sort()
    if any(a >= b for a, b in ranges) or any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise RuntimeError("Free/live physical ownership overlap")
    return pool, live, pins, header[2]


# ====== transition_memory ======

def transition_memory(vm, log, root, pool, live, pins):
    data, tables = (log.number("KERNEL_AP_TRANSITION_" + x) for x in ("DATA", "TABLES"))
    code = log.number("KERNEL_AP_LOW_BASE")
    owner = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_OWNER"), 19)
    if owner[17:] != [4 | (1 << 32), 1]:
        raise RuntimeError("Transition not frozen and pinned")
    for base, pages, ident in ((data, 1, owner[5]), (tables, 4, owner[9])):
        indices = [i for i in range(64) if live[i * 4:i * 4 + 4] == [base, pages, ident, pool]]
        if len(indices) != 1 or (pins[indices[0] // 2] >> (32 * (indices[0] % 2))) & 0xffffffff != 1:
            raise RuntimeError("Transition lease not uniquely owned/pinned")
    source = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_SOURCE"), 512)
    if any(vm.physical(root, 256)):
        raise RuntimeError("Operational lower half gained a mapping")
    for level in range(4):
        expected = [0] * 512
        if level == 0:
            expected[0] = tables + 4096 | 3
            expected[256:] = source[256:]
            expected[510] = 0
        elif level < 3:
            expected[0] = tables + (level + 1) * 4096 | 3
        else:
            expected[code >> 12], expected[data >> 12] = code | 1, data | (1 << 63) | 3
        if any((a ^ b) & ~0x60 for a, b in zip(vm.physical(tables + level * 4096, 512), expected)):
            raise RuntimeError("Frozen transition table permissions differ")
    if vm.physical(code, 512) != cpu.memory(vm, log.number("KERNEL_AP_IMAGE_CODE_EXPECTED"), 512):
        raise RuntimeError("Frozen trampoline code changed")
    actual = vm.physical(data, 512)
    expected = cpu.memory(vm, log.number("KERNEL_AP_IMAGE_DATA_EXPECTED"), 512)
    if actual[:13] != expected[:13] or actual[13] != 3 or actual[14:136] != expected[14:136]:
        raise RuntimeError("Final shared AP data differs")


# ====== verify ======

def verify(vm, log, count, panic=False, reason=None):
    failures = ["KERNEL_SMP_PANIC"] if panic else []
    if log.failures() != failures or log.count("KERNEL_READY", "OK") != int(not panic):
        raise RuntimeError("Ordinary boot did not complete cleanly")
    if log.has("KERNEL_AP_TRANSITION_TEST_READY") or log.has("KERNEL_AP_JOINT_RETENTION_GUARDS_OK"):
        raise RuntimeError("Diagnostic probe executed in ordinary boot")
    if count == 1:
        if log.count("KERNEL_AP_RUNTIME_BSP_ONLY", "OK") != 1 or log.has("KERNEL_AP_RUNTIME_READY"):
            raise RuntimeError("BSP-only path differs")
    elif log.count("KERNEL_AP_RUNTIME_READY", "OK") != 1 or not log.before("KERNEL_AP_RUNTIME_READY", "KERNEL_TIMER_OK"):
        raise RuntimeError("AP startup missing or BSP did not continue")
    texts = [launch.registers(vm, i) for i in range(count)]
    root = launch.value(texts[0], "CR3")
    terminal = 0x50414e4943444f4e if panic else 0x4b52454144590001
    if "HLT=1" not in texts[0] or launch.value(texts[0], "RAX") != terminal:
        raise RuntimeError("Actual BSP ordinary terminal marker differs")
    if cpu.gs_base(texts[0]) != log.number("KERNEL_PERCPU_BASE") or launch.value(texts[0], "RFL") & 0x200:
        raise RuntimeError("BSP GS/IF differs")
    cpu.identity(vm, log, count, log.number("KERNEL_PERCPU_CONTEXT"))
    arena = cpu.ap_monitor.verify(vm, log, cpu.memory, count, True, count - 1)
    transition.low.verify(vm, log, False, failures)
    pool, live, pins, free = allocator(vm, log)
    mask = (1 << count) - 1
    state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
    expected_state = [2, mask, mask, 1, mask - 1, mask - 1, 0, 2, 0] + [2] * count + [0] * (71 - count) if panic else [2, mask, mask] + [0] * 77
    if state != expected_state:
        raise RuntimeError("Panic admission not complete or already consumed")
    if count > 1:
        if [log.number("KERNEL_AP_RUNTIME_" + s) for s in ("DISCOVERED", "PARKED", "ONLINE")] != [count, count - 1, mask]:
            raise RuntimeError("Serial AP outcome differs")
        if root != log.number("KERNEL_AP_TRANSITION_ROOT"):
            raise RuntimeError("BSP left the shared operational root")
        first = log.number("KERNEL_AP_CONTEXTS")
        launch.verify_parked_cpus(vm, log, root, first, panic)
        transition_memory(vm, log, root, pool, live, pins)
        for index in range(1, count):
            address = first + (index - 1) * 448
            local = cpu.memory(vm, address, 56)
            if local[0] != address or local[2] != index | (2 << 32) or local[4] != 1 or local[6] or any(local[7:]):
                raise RuntimeError("Private parked AP context changed")
    if panic:
        verify_panic(vm, log, count, root, texts, reason)
    for index, text in enumerate(texts):
        (vm.directory / f"runtime-cpu-{index}.txt").write_text(text, encoding="utf-8")
    return dict(ConfiguredVcpus=count, OsCpusInitialized=count, ParkedAps=count - 1, SchedulerCpus=1, Panic=panic,
                Root=hex(root), FreePages=free, Arena=arena)


# ====== verify_panic ======

def verify_panic(vm, log, count, root, texts, expected_reason=None):
    mask = (1 << count) - 2
    for name, expected in (("OWNER", 0), ("TARGETS", mask), ("ACK", mask), ("MISSING", 0),
                           ("OUTCOME", 0), ("RECORD_COMPLETE", 1), ("KIND", 1), ("VECTOR", 0), ("ERROR", 0)):
        if log.number("PANIC_" + name) != expected:
            raise RuntimeError("Continuation panic report differs: " + name)
    for name, expected in (("OUTPUT", 1), ("RECURSION", 0)):
        if cpu.memory(vm, log.number("KERNEL_PANIC_" + name), 1) != [expected]:
            raise RuntimeError("Continuation panic output incomplete or recursive")
    for index in range(count):
        base = log.number("KERNEL_AP_BASE") + index * 0x40000 if index else 0
        address = base + 48 * 4096 + 256 if index else log.number("KERNEL_PANIC_RECORD")
        record = cpu.memory(vm, address, 64)
        if record[:3] != [address, 1, index] or record[7] != root or any(record[31:]):
            raise RuntimeError("Continuation panic record ownership/root/reserved differs")
        if record[11] != 8 or record[14] not in (0, 16) or record[12] & 0x7600 or not record[12] & 2:
            raise RuntimeError("Continuation captured ring/flags differ")
        if record[19] != record[13] or not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
            raise RuntimeError("Continuation captured RIP/RSP differs")
        if index:
            if record[3] != 3 or record[5] or record[8:10] != [2, 0] or not base + 25 * 4096 <= record[4] <= base + 29 * 4096 - 56:
                raise RuntimeError("Continuation follower NMI frame differs")
            if not base + 4096 <= record[13] <= base + 17 * 4096 - 1024:
                raise RuntimeError("Interrupted AP stack differs")
        else:
            if record[3] != 1 or record[8:10] != [0, 0]:
                raise RuntimeError("Continuation origin reason differs")
            if expected_reason is not None:
                if record[5] != expected_reason:
                    raise RuntimeError("Unexpected numeric panic reason")
            else:
                if not 0xffffffff80000000 <= record[5] <= 0xffffffff80400000 - 24:
                    raise RuntimeError("Continuation reason outside image")
                reason = b"".join(w.to_bytes(8, "little") for w in cpu.memory(vm, record[5], 3))
                if not reason.startswith(b"INTENTIONAL_TEST_PANIC\0"):
                    raise RuntimeError("Unexpected kernel failure masquerading as intentional panic")
            if not 0xfffff40000061000 <= record[4] <= 0xfffff40000071000 - 136 or record[13] != record[4] + 136:
                raise RuntimeError("Continuation BSP voluntary frame differs")


# ====== attest_console ======

def attest_console(vm, log, count):
    deadline = time.monotonic() + 2
    while "HLT=1" not in launch.registers(vm, 0):
        if time.monotonic() >= deadline:
            raise TimeoutError("Console cleanup did not reach BSP terminal")
    vm.command("stop")
    return verify(vm, log, count)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 11))
    parser.add_argument("--smp", type=int, required=True, choices=(1, 4))
    parser.add_argument("--panic", action="store_true")
    args = parser.parse_args()
    if args.panic and args.smp != 4:
        parser.error("Continuation panic requires the four-core group")
    out = transition.ROOT / "kernel/build" / ("ap-runtime-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for iteration in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{iteration}", args.image, "std", smp=args.smp) as vm:
                try:
                    if args.panic:
                        group_spec = importlib.util.spec_from_file_location("group_panic", Path(__file__).parents[2] / "panic/integration/group.py")
                        group = importlib.util.module_from_spec(group_spec)
                        group_spec.loader.exec_module(group)
                        log = group.wait(vm, 0)
                    else:
                        log = vm.wait("KERNEL_READY", 90)
                    deadline = time.monotonic() + 2
                    while "HLT=1" not in launch.registers(vm, 0):
                        if time.monotonic() >= deadline:
                            raise TimeoutError("BSP did not halt after ordinary completion")
                    vm.command("stop")
                    if args.panic:
                        group.settle(vm, 0)
                    results.append(verify(vm, log, args.smp, args.panic))
                except BaseException:
                    vm.command("stop")
                    for index in range(args.smp):
                        (vm.directory / f"failure-cpu-{index}.txt").write_text(launch.registers(vm, index), encoding="utf-8")
                    raise
            print(f"PASS: ordinary boot {iteration}/{args.boots}; initialized={args.smp}, scheduler=BSP", flush=True)
    except Exception as error:
        results.append(dict(Failure=str(error)))
        qemu.finish(out, "ap-runtime", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-runtime", args.image, results, len(results), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
