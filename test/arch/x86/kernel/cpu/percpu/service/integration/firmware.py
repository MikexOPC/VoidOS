"""Real AP fixed IPIs with private stacks and checked interrupt return."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("service_runtime", Path(__file__).parents[2] / "startup/runtime/firmware.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
launch, cpu, qemu, transition = runtime.launch, runtime.cpu, runtime.qemu, runtime.transition


# ====== register_value ======

def register_value(text, name):
    match = re.search(rf"\b{re.escape(name)}\s*=([0-9a-fA-F]+)\b", text)
    if not match:
        raise RuntimeError("Missing hardware register " + name)
    return int(match[1], 16)


# ====== await_terminal ======

def complete_snapshot(log):
    return type(log)(log.text[:log.text.rfind("\n") + 1])


def await_terminal(vm, case):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = complete_snapshot(vm.log())
        if not vm.alive() or any(item != "KERNEL_SMP_PANIC" for item in log.failures()):
            raise RuntimeError("Unexpected kernel/VM failure")
        if case == "ping" and log.failures():
            raise RuntimeError("Unexpected panic in positive service profile")
        ready = log.has("KERNEL_IPI_SERVICE_READY") if case == "ping" else "KERNEL_SMP_PANIC_HALTED" in log.text
        if ready:
            owner = 0 if case == "ping" else log.number("PANIC_OWNER")
            text = launch.registers(vm, owner)
            marker = 0x4b34495049504153 if case == "ping" else 0x50414e4943444f4e
            if "HLT=1" in text and launch.value(text, "RAX") == marker:
                return complete_snapshot(vm.log())
        time.sleep(0.005)
    raise TimeoutError("IPI service terminal missing")


# ====== verify ======

def verify(vm, log, case, count):
    texts = [launch.registers(vm, index) for index in range(count)]
    mask = (1 << count) - 1
    root = launch.value(texts[0], "CR3")
    if [log.number("KERNEL_AP_RUNTIME_" + item) for item in ("DISCOVERED", "PARKED", "ONLINE")] != [count, count - 1, mask]:
        raise RuntimeError("Actual AP startup missing")
    cpu.identity(vm, log, count, log.number("KERNEL_PERCPU_CONTEXT"))
    cpu.ap_monitor.verify(vm, log, cpu.memory, count, True, count - 1)
    pool, live, pins, free = runtime.allocator(vm, log)
    runtime.transition_memory(vm, log, root, pool, live, pins)
    address = log.number("KERNEL_IPI_NATIVE")
    idtr = b"".join(word.to_bytes(8, "little") for word in cpu.memory(vm, log.number("KERNEL_IPI_IDTR"), 2))
    if int.from_bytes(idtr[:2], "little") != 4095 or int.from_bytes(idtr[2:10], "little") != address:
        raise RuntimeError("Service IDTR geometry differs")
    gates = cpu.memory(vm, address, 512)
    panic_idtr = b"".join(word.to_bytes(8, "little") for word in cpu.memory(vm, log.number("KERNEL_AP_PANIC_IDTR"), 2))
    if gates[:64] != cpu.memory(vm, int.from_bytes(panic_idtr[2:10], "little"), 64):
        raise RuntimeError("Service IDT changed fault/NMI gates")
    for vector in range(32, 256):
        low, high = gates[vector * 2:vector * 2 + 2]
        target = (low & 0xffff) | ((low >> 48) << 16) | ((high & 0xffffffff) << 32)
        if (low >> 16) & 0xffffffff != 0x8e000008 or high >> 32 or not 0xffffffff80000000 <= target < 0xffffffff80400000:
            raise RuntimeError("Invalid service gate or target")
    counters = cpu.memory(vm, address + 4160, 64 * 8)
    first = log.number("KERNEL_AP_CONTEXTS")
    owner = log.number("PANIC_OWNER") if case != "ping" else None
    final = vm.physical(log.number("KERNEL_AP_TRANSITION_DATA"), 13)
    for index in range(count):
        text = texts[index]
        (vm.directory / f"cpu-{index}.txt").write_text(text, encoding="utf-8")
        if "HLT=1" not in text or launch.value(text, "CR3") != root:
            raise RuntimeError("CPU not halted on shared root")
        if launch.value(text, "CR0") & 0x80010001 != 0x80010001 or launch.value(text, "EFER") & 0xd00 != 0xd00 or launch.value(text, "CR4") & 0x40620 != 0x620:
            raise RuntimeError("Actual paging/WP/NX/FXSAVE prerequisites differ")
        if index == 0:
            if cpu.gs_base(text) != log.number("KERNEL_PERCPU_BASE") or launch.value(text, "RFL") & 0x7600:
                raise RuntimeError("BSP context or terminal IF differs")
            continue
        base = log.number("KERNEL_AP_BASE") + index * 0x40000
        top, meta = base + 17 * 4096 - 1024, base + 48 * 4096
        if cpu.gs_base(text) != first + (index - 1) * 448 or not re.search(rf"GDT=\s*{meta:016x}\s+00000027", text, re.I):
            raise RuntimeError("AP private context/GDT differs")
        if not re.search(rf"IDT=\s*{address:016x}\s+00000fff", text, re.I) or not re.search(r"(?m)^TR\s*=\s*0018", text, re.I):
            raise RuntimeError("AP IDTR/TR differs")
        frame = cpu.memory(vm, top, 128)
        expected = list(final)
        expected[1], expected[2], expected[7:10] = index | index << 32, index << 9 | index << 3, [top + 1024, meta, first + (index - 1) * 448]
        if frame[:13] != expected or frame[83] != 6:
            raise RuntimeError("Sealed AP startup record changed")
        if case == "ping":
            values = counters[index * 8:(index + 1) * 8]
            if values != [100, 100, values[2], top - 576, 100, 100, 0, 0] or values[2] < 1:
                raise RuntimeError("IPI completion/return/EOI counters differ: " + str(values))
            if launch.value(text, "RSP") != top - 576 or launch.value(text, "RFL") & 0x7602 != 0x602:
                raise RuntimeError("AP did not return to IF=1/DF=1 private idle")
            for ordinal, register in enumerate(("RAX", "RBX", "RCX", "RDX", "RBP", "RSI", "RDI", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"), 1):
                if register_value(text, register) != 0x12345000 + ordinal:
                    raise RuntimeError("Actual restored GPR differs: " + register)
        else:
            marker = 0x50414e4943444f4e if index == owner else 0x50414e494353544f
            lower, upper = (base + 4096, top) if index == owner else (base + 25 * 4096, base + 29 * 4096)
            if launch.value(text, "RAX") != marker or not lower <= launch.value(text, "RSP") < upper:
                raise RuntimeError("AP did not stop through private NMI IST")
    state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
    if case == "ping":
        if log.failures() or state != [2, mask, mask] + [0] * 77:
            raise RuntimeError("Unexpected panic state")
    else:
        expected_owner = 1 if case == "stale-ack" else 0
        targets = mask & ~(1 << expected_owner)
        if owner != expected_owner or state[:9] != [2, mask, mask, owner + 1, targets, targets, 0, 2, 0] or state[9:] != [2] * count + [0] * (71 - count):
            raise RuntimeError("SMP panic coordinator did not collect all CPUs")
        reason = {"missing-ack": 0x4b34495049544d4f, "stale-ack": 0x4b34495049424144,
                  "panic-idle": 0x4b344950494e4d34, "panic-handler": 0x4b344950494e4d35}[case]
        if log.number("PANIC_REASON_ADDRESS") != reason or log.number("PANIC_MISSING") != 0:
            raise RuntimeError("Different failure reason or missing panic ACK")
        for index in range(count):
            base = log.number("KERNEL_AP_BASE") + index * 0x40000
            record_address = base + 48 * 4096 + 256 if index else log.number("KERNEL_PANIC_RECORD")
            record = cpu.memory(vm, record_address, 64)
            if record[:3] != [record_address, 1, index] or record[7] != root or any(record[31:]):
                raise RuntimeError("Panic record ownership/root/reserved differs")
            expected_kind, expected_vector = (1, 0) if index == owner else (3, 2)
            if record[3] != expected_kind or record[5] != (reason if index == owner else 0) or record[8:10] != [expected_vector, 0]:
                raise RuntimeError("Captured panic cause differs")
            if record[11] != 8 or record[14] not in (0, 16) or not record[12] & 2 or record[12] & 0x7000 or record[19] != record[13]:
                raise RuntimeError("Captured privilege/flags/GPR RSP differs")
            if not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
                raise RuntimeError("Captured panic RIP outside kernel")
            if case == "panic-handler" and index == 1 and record[12] & 0x600:
                raise RuntimeError("AP1 was not captured inside IF=0/DF=0 handler")
            if case == "panic-idle" and index and record[12] & 0x600 != 0x600:
                raise RuntimeError("AP was not captured in interruptible idle")
        if cpu.memory(vm, log.number("KERNEL_PANIC_OUTPUT"), 1) != [1] or cpu.memory(vm, log.number("KERNEL_PANIC_RECURSION"), 1) != [0]:
            raise RuntimeError("Incomplete emergency report")
        if case == "missing-ack" and log.number("KERNEL_IPI_FAILED_PHASE") != 2:
            raise RuntimeError("Timed-out mailbox was reopened")
    return dict(Case=case, RealCpus=count, Requests=100 * (count - 1) if case in ("ping", "panic-idle") else 1,
                PhysicalFree=free, Pinned=True)


# ====== capture_failure ======

def capture_failure(vm, count):
    vm.command("stop")
    for index in range(count):
        (vm.directory / f"failure-cpu-{index}.txt").write_text(launch.registers(vm, index), encoding="utf-8")


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--case", choices=("ping", "missing-ack", "stale-ack", "panic-idle", "panic-handler"), default="ping")
    parser.add_argument("--boots", type=int, choices=range(1, 11), default=10)
    parser.add_argument("--smp", type=int, choices=range(2, 65), default=4)
    args = parser.parse_args()
    out = transition.ROOT / "kernel/build" / ("ipi-service-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    passed = 0
    try:
        for iteration in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{iteration}", args.image, "std", smp=args.smp) as vm:
                try:
                    log = await_terminal(vm, args.case)
                    vm.command("stop")
                    if args.case != "ping":
                        group_spec = importlib.util.spec_from_file_location("service_group", Path(__file__).parents[2] / "panic/integration/group.py")
                        group = importlib.util.module_from_spec(group_spec)
                        group_spec.loader.exec_module(group)
                        group.settle(vm, log.number("PANIC_OWNER"), args.smp)
                        log = vm.log()
                    results.append(verify(vm, log, args.case, args.smp))
                except BaseException as original:
                    try:
                        capture_failure(vm, args.smp)
                    except Exception as capture_error:
                        original.add_note(f"Diagnostic capture also failed: {capture_error}")
                    raise
            passed += 1
            print(f"PASS: IPI {args.case} {iteration}/{args.boots}; {args.smp} CPUs, actual AP interrupt delivery", flush=True)
    except Exception as error:
        results.append(dict(Failure=str(error)))
        qemu.finish(out, "ipi-service", args.image, results, passed, args.boots)
        raise
    if not qemu.finish(out, "ipi-service", args.image, results, passed, args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
