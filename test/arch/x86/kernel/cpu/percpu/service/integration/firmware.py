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
lot_spec = importlib.util.spec_from_file_location("concurrent_batch", Path(__file__).parents[4] / "scheduler/batch/concurrent.py")
concurrent_batch = importlib.util.module_from_spec(lot_spec)
lot_spec.loader.exec_module(concurrent_batch)


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
    positive = case in ("ping", "ap-timer", "admission", "ap-batch", "ap-preempt", "ap-concurrent", "ap-concurrent-stress", "ap-four")
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = complete_snapshot(vm.log())
        if not vm.alive() or any(item != "KERNEL_SMP_PANIC" for item in log.failures()):
            raise RuntimeError("Unexpected kernel/VM failure")
        if positive and log.failures():
            raise RuntimeError("Unexpected panic in positive service profile")
        ready_marker = {"ap-timer": "KERNEL_AP_TIMER_READY", "admission": "KERNEL_AP_DISPATCH_ADMITTED", "ap-batch": "KERNEL_AP_BATCH_READY", "ap-preempt": "KERNEL_AP_PREEMPT_READY", "ap-concurrent": "KERNEL_AP_CONCURRENT_READY", "ap-concurrent-stress": "KERNEL_AP_CONCURRENT_READY", "ap-four": "KERNEL_AP_FOUR_READY"}.get(case,"KERNEL_IPI_SERVICE_READY")
        ready = log.has(ready_marker) if positive else "KERNEL_SMP_PANIC_HALTED" in log.text
        if ready and case == "ping" and not log.before("KERNEL_IPI_REMAP_VERIFIED", "KERNEL_IPI_SERVICE_READY"):
            raise RuntimeError("Missing AP remap and release evidence")
        if ready:
            owner = 0 if positive else log.number("PANIC_OWNER")
            text = launch.registers(vm, owner)
            marker = 0x4b34495049504153 if positive else 0x50414e4943444f4e
            if "HLT=1" in text and launch.value(text, "RAX") == marker:
                return complete_snapshot(vm.log())
        time.sleep(0.005)
    raise TimeoutError("IPI service terminal missing")


# ====== verify ======

def verify(vm, log, case, count):
    positive = case in ("ping", "ap-timer", "admission", "ap-batch", "ap-preempt", "ap-concurrent", "ap-concurrent-stress", "ap-four")
    texts = [launch.registers(vm, index) for index in range(count)]
    mask = (1 << count) - 1
    root = launch.value(texts[0], "CR3")
    if [log.number("KERNEL_AP_RUNTIME_" + item) for item in ("DISCOVERED", "PARKED", "ONLINE")] != [count, count - 1, mask]:
        raise RuntimeError("Actual AP startup missing")
    cpu.identity(vm, log, count, log.number("KERNEL_PERCPU_CONTEXT"))
    cpu.ap_monitor.verify(vm, log, cpu.memory, count, True, count - 1)
    pool, live, pins, free = runtime.allocator(vm, log)
    runtime.transition_memory(vm, log, root, pool, live, pins)
    if case in ("ping", "missing-ack", "stale-ack"):
        old_frame = log.number("KERNEL_IPI_OLD_FRAME")
        new_frame = log.number("KERNEL_IPI_NEW_FRAME")
        if old_frame == new_frame:
            raise RuntimeError("Diagnostic frames overlap")
        for frame, expected in ((old_frame, case != "ping"), (new_frame, case != "ping")):
            indices = [index for index in range(64)
                       if live[4 * index] == frame and live[4 * index + 2]]
            if len(indices) != int(expected):
                raise RuntimeError("Diagnostic lease released or retained at the wrong time")
            if expected:
                index = indices[0]
                if live[4 * index + 1] != 1 or (pins[index // 2] >> (32 * (index % 2))) & 0xffffffff != 1:
                    raise RuntimeError("Diagnostic frame is not a pinned single-page lease")
    if case == "release-missing-ack":
        frame = log.number("KERNEL_IPI_RELEASE_FRAME")
        indices = [index for index in range(64) if live[4 * index] == frame and live[4 * index + 2]]
        if len(indices) != 1:
            raise RuntimeError("Released window frame returned before the flush ACK")
        index = indices[0]
        if live[4 * index + 1] != 1 or (pins[index // 2] >> (32 * (index % 2))) & 0xffffffff != 1:
            raise RuntimeError("Retained window frame is not a pinned single-page lease")
        if vm.physical(frame, 1)[0] & 0xff != 0x5a:
            raise RuntimeError("Retained window frame content changed")
    if case == "ping":
        dma_frames = [log.number("KERNEL_IPI_DMA_DATA"), *log.numbers("KERNEL_IPI_DMA_TABLE")]
        if len(dma_frames) != 4 or len(set(dma_frames)) != 4:
            raise RuntimeError("Diagnostic DMA leases differ")
        framebuffer_tables = log.numbers("KERNEL_IPI_FB_TABLE")
        if (not framebuffer_tables or len(framebuffer_tables) != log.number("KERNEL_IPI_FB_TABLE_COUNT") or
                len(set(framebuffer_tables)) != len(framebuffer_tables)):
            raise RuntimeError("Diagnostic framebuffer leases differ")
        frames = dma_frames + framebuffer_tables + [log.number("KERNEL_IPI_" + name)
                               for name in ("REGION_FRAME", "HEAP_DATA", "HEAP_TABLES")]
        live_bases = {live[index] for index in range(0, len(live), 4) if live[index + 2]}
        if 0 in frames or live_bases.intersection(frames):
            raise RuntimeError("Diagnostic lease was not released")
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
    generation_base = log.number("KERNEL_IPI_BASE_GENERATION")
    if case in ("missing-ack", "stale-ack") and [counters[index] for index in (8, 12, 13)] != [generation_base + 99] * 3:
        raise RuntimeError("AP1 did not enter and finish EOI for the flush request")
    if case == "release-missing-ack":
        for index in range(1, count):
            expected = generation_base + (101 if index == 1 else 100)
            if [counters[index * 8 + field] for field in (0, 4, 5)] != [expected] * 3:
                raise RuntimeError("Release flush delivery, EOI or entry counters differ")
    first = log.number("KERNEL_AP_CONTEXTS")
    owner = None if positive else log.number("PANIC_OWNER")
    timer_ticks = log.numbers("KERNEL_AP_TIMER_TICKS")
    if case == "ap-timer" and (len(timer_ticks) != count - 1 or min(timer_ticks) < 3 or
                              log.number("KERNEL_AP_TIMER_RELOAD") != log.number("KERNEL_TIMER_RELOAD")):
        raise RuntimeError("Missing timer progress or BSP calibration")
    if case == "admission":
        record_address = log.number("KERNEL_AP_DISPATCH_RECORD")
        record = cpu.memory(vm,record_address,8)
        ap_base = log.number("KERNEL_AP_BASE") + 0x40000
        top = ap_base + 17 * 4096 - 1024
        local = record[5]
        if (record[:4] != [record_address,root,1,1] or record[6:] != [root,0] or
                not ap_base + 8192 <= record[4] < top - 576 or local & 63 or
                not 0xffffffff80000000 <= local < 0xffffffff80400000 - 704):
            raise RuntimeError("AP admission native evidence differs")
        words = cpu.memory(vm,local,88)
        expected_local = [0] * 88
        expected_local[80] = 0xffffffff00000000
        expected_local[82:84] = [local,first]
        if words != expected_local:
            raise RuntimeError("AP dispatcher context identity or initial state differs")
    if case in ("ap-batch", "ap-preempt"):
        record_address = log.number("KERNEL_AP_BATCH_RECORD")
        record = cpu.memory(vm,record_address,32)
        if (record[:2] != [record_address,root] or record[2] != pool or
                record[3] < 32 or record[4] != record[3] - 32 or record[5] != record[3] or free != record[5] or
                record[6:12] != [16,16,136,272,17,34] or record[12] < 2 or
                not record[13] or record[13] & ~3 or record[15] >= 2 or
                not record[17] or not record[18] or record[16] != record[17 + record[15]] or
                not (8 if case == "ap-preempt" else 34) <= record[19] <= 8192 or record[20] != 2 or record[26:28] != [2,2] or
                record[28] & 0x7602 != 0x202 or record[29:] != [0xe1,1,0]):
            raise RuntimeError("AP task progress, context, IRQ or cleanup evidence differs: " + str(record))
        base = record[21 + record[15]]
        if not base + 4096 <= record[14] < base + 65536:
            raise RuntimeError("IRQ did not interrupt an owned AP task stack")
        if record[22] - record[21] != 18 * 4096 or any(cpu.memory(vm,log.number("KERNEL_AP_BATCH_PTE"),36)):
            raise RuntimeError("AP task stack geometry or final unmap differs")
        local = cpu.memory(vm,record[23],88)
        if (record[23] & 63 or local[74:78] != [record[19] | 2 << 32,0,0,0] or
                local[78:82] != [0,0,0xffffffff00000000,0] or local[82:84] != [record[23],first]):
            raise RuntimeError("AP dispatcher did not return without a current task")
    if case == "ap-preempt":
        preempt_address = log.number("KERNEL_AP_PREEMPT_RECORD")
        preempt = cpu.memory(vm,preempt_address,16)
        if (preempt[:7] != [preempt_address,3,3,3,3,6,1] or preempt[7] > 8 or
                not 0 < preempt[14] <= 0xffffffff or preempt[15]):
            raise RuntimeError("AP preemption count or stop differs: " + str(preempt))
        for task in range(2):
            base = record[21 + task]
            if (not base + 4096 <= preempt[8 + task] < base + 65536 - 48 or
                    not base + 4096 <= preempt[10 + task] < base + 65536 or
                    not 0xffffffff80000000 <= preempt[12 + task] < 0xffffffff80400000):
                raise RuntimeError("AP preemption frame is not on its owned task stack")
    if case in ("ap-concurrent", "ap-concurrent-stress", "ap-four"):
        target, rounds = (8, 8) if case in ("ap-concurrent-stress", "ap-four") else (3, 1)
        concurrent_batch.verify(vm, log, count, root, pool, free, first, cpu.memory, target, rounds, 8 if case == "ap-four" else 4)
    if case in ("ap-concurrent-incomplete", "ap-four-incomplete"):
        concurrent_batch.verify_incomplete(vm, log, root, pool, free, live, pins, cpu.memory, 8 if case == "ap-four-incomplete" else 4)
    if case == "ap-batch-rollback":
        record_address = log.number("KERNEL_AP_BATCH_RECORD")
        record = cpu.memory(vm,record_address,32)
        if (not log.before("KERNEL_AP_BATCH_ROLLBACK_OK","KERNEL_SMP_PANIC") or
                record[:2] != [record_address,root] or record[2] != pool or record[3] != free or record[4] != record[3] - 16 or
                record[5] != free or record[31] != 2 or record[6:17] != [0] * 11 or record[19:21] != [0,0] or
                any(cpu.memory(vm,log.number("KERNEL_AP_BATCH_PTE"),36))):
            raise RuntimeError("AP batch rollback did not restore resources before panic")
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
        if positive:
            expected_generation = generation_base + ((29 if index <= 4 else 19) if case == "ap-four" else (25 if index in (1, 2) else 15) if case == "ap-concurrent-stress" else (11 if index in (1, 2) else 8) if case == "ap-concurrent" else 4 if case == "ap-timer" else (4 if index == 1 else 3) if case == "admission" else (9 if index == 1 else 6) if case in ("ap-batch", "ap-preempt") else 105)
            values = counters[index * 8:(index + 1) * 8]
            if values != [expected_generation, expected_generation, values[2], top - 576, expected_generation, expected_generation, 0, 0] or values[2] < 1:
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
    if positive:
        if log.failures() or state != [2, mask, mask] + [0] * 77:
            raise RuntimeError("Unexpected panic state")
    else:
        expected_owner = 1 if case == "stale-ack" else 0
        targets = mask & ~(1 << expected_owner)
        if owner != expected_owner or state[:9] != [2, mask, mask, owner + 1, targets, targets, 0, 2, 0] or state[9:] != [2] * count + [0] * (71 - count):
            raise RuntimeError("SMP panic coordinator did not collect all CPUs")
        reason = {"missing-ack": 0x4b34495049544d4f, "stale-ack": 0x4b34495049424144,
                  "panic-idle": 0x4b344950494e4d34, "panic-handler": 0x4b344950494e4d35,
                  "release-missing-ack": 0x4b34544c4241434b, "ap-batch-rollback": 0x4b34415042415443, "ap-concurrent-incomplete": 0x4b34415042415443, "ap-four-incomplete": 0x4b34415042415443}[case]
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
    requests = 100 * (count - 1) if case in ("ping", "panic-idle") else 1
    if case == "ping":
        requests += 5 * (count - 1)
    if case in ("missing-ack", "stale-ack"):
        requests += 98 * (count - 1)
    if case == "release-missing-ack":
        requests = 100 * (count - 1) + 1
    if case == "ap-timer":
        requests = 4 * (count - 1)
    if case == "admission":
        requests = 3 * (count - 1) + 1
    if case in ("ap-batch", "ap-preempt"):
        requests = 6 * (count - 1) + 3
    if case == "ap-concurrent":
        requests = 8 * (count - 1) + 6
    if case == "ap-concurrent-stress":
        requests = 15 * (count - 1) + 20
    if case == "ap-concurrent-incomplete":
        requests = 3 * (count - 1) + 6
    if case == "ap-four":
        requests = 19 * (count - 1) + 40
    if case == "ap-four-incomplete":
        requests = 10 * (count - 1 + 4)
    return dict(Case=case, RealCpus=count, Requests=requests, TimerTicks=timer_ticks,
                PhysicalFree=free, Pinned=True, AdmittedCpus=[1,2,3,4] if case in ("ap-four", "ap-four-incomplete") else [1,2] if case in ("ap-concurrent", "ap-concurrent-stress", "ap-concurrent-incomplete") else [1] if case in ("admission", "ap-batch", "ap-preempt") else [],
                TaskCpus=[1,2,3,4] if case in ("ap-four", "ap-four-incomplete") else [1,2] if case in ("ap-concurrent", "ap-concurrent-stress", "ap-concurrent-incomplete") else [1] if case in ("ap-batch", "ap-preempt") else [])


# ====== capture_failure ======

def capture_failure(vm, count):
    vm.command("stop")
    for index in range(count):
        (vm.directory / f"failure-cpu-{index}.txt").write_text(launch.registers(vm, index), encoding="utf-8")


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--case", choices=("ping", "missing-ack", "stale-ack", "panic-idle", "panic-handler", "release-missing-ack", "ap-timer", "admission", "ap-batch", "ap-batch-rollback", "ap-preempt", "ap-concurrent", "ap-concurrent-stress", "ap-concurrent-incomplete", "ap-four", "ap-four-incomplete"), default="ping")
    parser.add_argument("--boots", type=int, choices=range(1, 11), default=10)
    parser.add_argument("--smp", type=int, choices=range(2, 65), default=4)
    args = parser.parse_args()
    if args.case in ("ap-four", "ap-four-incomplete") and args.smp < 5:
        parser.error("Four AP workers require at least five total CPUs")
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
                    if args.case not in ("ping", "ap-timer", "admission", "ap-batch", "ap-preempt", "ap-concurrent", "ap-concurrent-stress", "ap-four"):
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
