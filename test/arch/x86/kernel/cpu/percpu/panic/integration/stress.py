"""Broadcast NMI, duplicate delivery, missing ACK and unavailable serial on real parked cores."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("stress_runtime", Path(__file__).parents[2] / "startup/runtime/firmware.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
launch, cpu, qemu = runtime.launch, runtime.cpu, runtime.qemu
DONE, STOP = 0x50414e4943444f4e, 0x50414e494353544f


# ====== state ======

def state(vm, log):
    return cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)


# ====== wait_checkpoint ======

def wait_checkpoint(vm, marker):
    deadline = time.monotonic() + 90
    while True:
        log = vm.log()
        if log.has(marker, "OK"):
            return log
        if time.monotonic() >= deadline or not vm.alive() or log.failures():
            raise RuntimeError("Panic checkpoint missing: " + marker)
        time.sleep(0.005)


# ====== wait_terminal ======

def wait_terminal(vm, log, serial):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if not vm.alive():
            raise RuntimeError("Owned VM exited before coordinated halt")
        current = state(vm, log)
        if current[7] == 2 and 1 <= current[3] <= 4:
            owner = current[3] - 1
            text = launch.registers(vm, owner)
            if "HLT=1" in text and launch.value(text, "RAX") == DONE:
                if not serial or "KERNEL_SMP_PANIC_HALTED" in vm.log().text:
                    return owner
        time.sleep(0.005)
    raise TimeoutError("Panic did not complete with a real reporter HLT")


# ====== captures ======

def captures(vm, log):
    addresses = [log.number("KERNEL_PANIC_RECORD")]
    base = log.number("KERNEL_AP_BASE")
    addresses += [base + i * 0x40000 + 48 * 4096 + 256 for i in range(1, 4)]
    return [cpu.memory(vm, address, 64) for address in addresses]


# ====== verify_records ======

def verify_records(vm, log, root, owner, before, frames):
    records = captures(vm, log)
    contenders = 0
    for index, record in enumerate(records):
        old = before[index] if before is not None else None
        text = launch.registers(vm, index)
        (vm.directory / f"stress-cpu-{index}.txt").write_text(text, encoding="utf-8")
        if "HLT=1" not in text or launch.value(text, "RAX") != (DONE if index == owner else STOP):
            raise RuntimeError("Actual CPU terminal marker differs")
        gs = (cpu.gs_base(old) if old else log.number("KERNEL_AP_CONTEXTS") + (index - 1) * 448 if index else log.number("KERNEL_PERCPU_BASE"))
        if launch.value(text, "CR3") != root or cpu.gs_base(text) != gs or launch.value(text, "RFL") & 0x7600:
            raise RuntimeError("Actual CPU root/GS/IF differs")
        if launch.value(text, "CR0") & 0x80010001 != 0x80010001 or launch.value(text, "EFER") & 0xd00 != 0xd00 or launch.value(text, "CR4") & 0x620 != 0x620:
            raise RuntimeError("Actual paging/WP/NX/SSE prerequisites differ")
        if old:
            for descriptor in ("GDT", "IDT", "TR"):
                pattern = rf"(?m)^{descriptor}\s*=.*$"
                if re.search(pattern, text)[0] != re.search(pattern, old)[0]:
                    raise RuntimeError("Private descriptor state changed")
        elif not index:
            for register, suffix in (("GDT", "GDTR"), ("IDT", "IDTR")):
                raw = b"".join(w.to_bytes(8, "little") for w in cpu.memory(vm, log.number("KERNEL_AP_STRESS_BSP_" + suffix), 2))
                base, limit = int.from_bytes(raw[2:10], "little"), int.from_bytes(raw[:2], "little")
                if not re.search(rf"{register}=\s*{base:016x}\s+{limit:08x}", text, re.I):
                    raise RuntimeError("Actual BSP descriptor differs from prepared panic context")
        address = log.number("KERNEL_PANIC_RECORD") if not index else log.number("KERNEL_AP_BASE") + index * 0x40000 + 48 * 4096 + 256
        if record[:3] != [address, 1, index] or record[7] != root or any(record[31:]):
            raise RuntimeError("Captured record owner/root/reserved differs")
        if record[3] not in (2, 3) or record[5] or record[8:10] != [2, 0] or (index == owner and record[3] != 2):
            raise RuntimeError("NMI origin/follower record differs")
        contenders += int(record[3] == 2)
        if record[11] != 8 or record[14] not in (0, 16) or record[12] & 0x7600 or not record[12] & 2:
            raise RuntimeError("Captured ring/flags differ")
        if record[19] != record[13] or not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
            raise RuntimeError("Captured RIP/GPR RSP differs")
        if index:
            base = log.number("KERNEL_AP_BASE") + index * 0x40000
            ist_base, ist_top = base + 25 * 4096, base + 29 * 4096
            main_base, main_top = base + 4096, base + 17 * 4096 - 1024
            if frames is not None and cpu.memory(vm, main_top, 128) != frames[index - 1]:
                raise RuntimeError("Parked AP private frame changed")
        else:
            ist_top = log.number("KERNEL_IST2_TOP")
            ist_base = ist_top - 16384
            main_base, main_top = 0xfffff40000061000, 0xfffff40000071000
        if not ist_base <= record[4] <= ist_top - 56 or not ist_base <= launch.value(text, "RSP") < ist_top:
            raise RuntimeError("NMI capture/terminal outside private IST2")
        if not main_base <= record[13] <= main_top:
            raise RuntimeError("NMI interrupted the wrong main stack")
    return records, contenders


# ====== verify ======

def verify(vm, initial, owner, case, before, frames, budget):
    current = state(vm, initial)
    targets = 15 & ~(1 << owner)
    missing = 1 << ((owner + 1) % 4) if case == "missing-ack" else 0
    outcome = current[8]
    if current[:8] != [2, 15, 15, owner + 1, targets, targets & ~missing, missing, 2]:
        raise RuntimeError("Owner/targets/ACK/missing/phase differ")
    if (not missing and outcome) or (missing and (not outcome & 1 or not outcome & (8 | 16) or outcome & ~(1 | 8 | 16))):
        raise RuntimeError("Wrong bounded-wait outcome")
    if current[9:13] != [2] * 4 or any(current[13:]):
        raise RuntimeError("Incomplete or stray CPU records")
    output = cpu.memory(vm, initial.number("KERNEL_PANIC_OUTPUT"), 1)[0]
    recursion = cpu.memory(vm, initial.number("KERNEL_PANIC_RECURSION"), 1)[0]
    if output != (2 if case == "serial-dlab" else 1) or recursion:
        raise RuntimeError("Serial failure/recursion state differs")
    root = launch.value(before[0], "CR3") if before else initial.number("KERNEL_AP_TRANSITION_ROOT")
    records, contenders = verify_records(vm, initial, root, owner, before, frames)
    if before is None:
        launch.verify_parked_cpus(vm, initial, root, initial.number("KERNEL_AP_CONTEXTS"), panic=True, reporter=owner)
    pool, live, pins, free = runtime.allocator(vm, initial)
    if free != budget:
        raise RuntimeError("Panic allocated or released physical pages")
    runtime.transition_memory(vm, initial, root, pool, live, pins)
    cpu.ap_monitor.verify(vm, initial, cpu.memory, 4, True, 3)
    if case != "serial-dlab":
        log = vm.log()
        if log.failures() != ["KERNEL_SMP_PANIC"]:
            raise RuntimeError("Unexpected panic log failure")
        for name, expected in (("OWNER", owner), ("TARGETS", targets), ("ACK", targets & ~missing),
                               ("MISSING", missing), ("OUTCOME", outcome), ("KIND", 2), ("VECTOR", 2), ("RECORD_COMPLETE", 1)):
            if log.number("PANIC_" + name) != expected:
                raise RuntimeError("Serial report disagrees with RAM")
    return dict(Owner=owner, ContendersObserved=contenders, Targets=targets, Ack=current[5], Missing=missing,
                Outcome=outcome, Output=output, Records=records, FreePages=free)


# ====== boot ======

def boot(image, out, index, case):
    with qemu.Vm(out / f"boot-{index}", image, "std", smp=4, memory=128 if index % 2 else 256) as vm:
        try:
            log = vm.wait("KERNEL_READY", 90)
            vm.command("stop")
            proof = runtime.verify(vm, log, 4)
            before = [launch.registers(vm, i) for i in range(4)]
            allocator_before = runtime.allocator(vm, log)
            frames = [cpu.memory(vm, log.number("KERNEL_AP_BASE") + i * 0x40000 + 17 * 4096 - 1024, 128) for i in range(1, 4)]
            if case == "serial-dlab":
                # QEMU v11.1.0 hw/char/serial.c: LCR +3, DLAB7; keep WLS=3 (8N1).
                vm.command("o /b 0x3fb 0x83")
            # QEMU v11.1.0 hw/i386/x86.c:x86_nmi ignores monitor CPU and targets all CPUs.
            vm.command("nmi")
            vm.command("cont")
            owner = wait_terminal(vm, log, case != "serial-dlab")
            vm.command("stop")
            deadline = time.monotonic() + 2
            while True:
                texts = [launch.registers(vm, i) for i in range(4)]
                if all("HLT=1" in t and launch.value(t, "RAX") == (DONE if i == owner else STOP) for i, t in enumerate(texts)):
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("Follower did not halt")
                vm.command("cont"); time.sleep(0.005); vm.command("stop")
            result = verify(vm, log, owner, case, before, frames, proof["FreePages"])
            if runtime.allocator(vm, log) != allocator_before:
                raise RuntimeError("Panic changed live leases or pin accounting")
            frozen = state(vm, log), captures(vm, log)
            vm.command("nmi"); vm.command("cont")
            time.sleep(0.05)
            vm.command("stop")
            if (state(vm, log), captures(vm, log)) != frozen:
                raise RuntimeError("Repeated NMI overwrote terminal report/records")
            for i in range(4):
                text = launch.registers(vm, i)
                if "HLT=1" not in text or launch.value(text, "RAX") != (DONE if i == owner else STOP):
                    raise RuntimeError("Repeated NMI left terminal state")
            result.update(Case=case, ConfiguredVcpus=4, OsCpusInitialized=4, DuplicateNmiStable=True)
            return result
        except BaseException:
            vm.command("stop")
            for i in range(4):
                (vm.directory / f"failure-cpu-{i}.txt").write_text(launch.registers(vm, i), encoding="utf-8")
            raise


# ====== boot_contended ======

def boot_contended(image, out, index):
    with qemu.Vm(out / f"boot-{index}", image, "std", smp=4) as vm:
        log = wait_checkpoint(vm, "KERNEL_AP_STRESS_IPI_ARMED")
        try:
            owner = wait_terminal(vm, log, True)
            vm.command("stop")
            symbols = image.with_suffix('.map').read_text(encoding='utf-8')
            for name, expected in (("xp_stress_arrived", 15), ("xp_stress_failed", 0)):
                match = re.search(rf"(?m)^\s*\S+\s+{name}\s+([0-9a-fA-F]{{16}})\b", symbols)
                address = int(match[1], 16) if match else 0
                if address & 7 or not 0xffffffff80000000 <= address <= 0xffffffff80400000 - 8 or cpu.memory(vm, address, 1) != [expected]:
                    raise RuntimeError("Contended diagnostic rendezvous incomplete")
            result = verify(vm, log, owner, "contended", None, None, log.number("KERNEL_FB_BUDGET_LIVE"))
            if result["ContendersObserved"] != 4:
                raise RuntimeError("Four independent origin captures were not observed")
            result.update(Case="contended", ConfiguredVcpus=4, OsCpusInitialized=4)
            return result
        except BaseException:
            vm.command("stop")
            for i in range(4):
                (vm.directory / f"failure-cpu-{i}.txt").write_text(launch.registers(vm, i), encoding="utf-8")
            raise


# ====== boot_lock ======

def boot_lock(image, out, index):
    with qemu.Vm(out / f"boot-{index}", image, "std", smp=4) as vm:
        log = wait_checkpoint(vm, "KERNEL_AP_STRESS_LOCK_HELD")
        owner = wait_terminal(vm, log, True)
        if owner:
            raise RuntimeError("Held-lock panic did not originate on BSP")
        vm.command("stop")
        proof = runtime.verify(vm, vm.log(), 4, panic=True, reason=(1 << 64) - 1)
        address, guard = log.number("KERNEL_AP_STRESS_LOCK"), log.number("KERNEL_AP_STRESS_GUARD")
        context = log.number("KERNEL_PERCPU_CONTEXT")
        lock = cpu.memory(vm, address, 4)
        local = cpu.memory(vm, context, 52)
        held = cpu.memory(vm, guard, 6)
        if lock != [address, 1, 0, 300 | (1 << 32)] or local[3] != 1:
            raise RuntimeError("Actual ticket lock not retained and owned")
        if held != [guard, context, address, 0, local[7], 0] or local[4:10] != [address, guard, 0, local[7], 0, 300]:
            raise RuntimeError("Actual guard/context lock frame differs")
        frozen = state(vm, log), captures(vm, log)
        vm.command("nmi"); vm.command("cont")
        deadline = time.monotonic() + 2
        while True:
            text = launch.registers(vm, 0)
            if "HLT=1" in text and launch.value(text, "RAX") == 0x50414e4943524543:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("Reporter recursive NMI did not stop")
            time.sleep(0.005)
        vm.command("stop")
        if (state(vm, log), captures(vm, log)) != frozen or cpu.memory(vm, log.number("KERNEL_PANIC_RECURSION"), 1) != [1]:
            raise RuntimeError("Recursive NMI overwrote the original report")
        for cpu_index in (1, 2, 3):
            text = launch.registers(vm, cpu_index)
            if "HLT=1" not in text or launch.value(text, "RAX") != STOP:
                raise RuntimeError("Follower escaped halt during recursion")
        return dict(Case="lock-held", ConfiguredVcpus=4, OsCpusInitialized=4, Owner=0, Lock=hex(address),
                    Guard=hex(guard), RecursiveNmiPreserved=True, FreePages=proof["FreePages"])


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--case", choices=("broadcast", "missing-ack", "serial-dlab", "lock-held", "contended"), required=True)
    parser.add_argument("--boots", type=int, choices=range(1, 21), default=10)
    args = parser.parse_args()
    out = runtime.transition.ROOT / "kernel/build" / ("ap-panic-stress-" + uuid.uuid4().hex)
    out.mkdir(); rows = []
    try:
        for index in range(1, args.boots + 1):
            run = boot_lock if args.case == "lock-held" else boot_contended if args.case == "contended" else None
            rows.append(run(args.image, out, index) if run else boot(args.image, out, index, args.case))
            print(f"PASS: {args.case} {index}/{args.boots}; owner={rows[-1]['Owner']}, missing={rows[-1].get('Missing',0)}", flush=True)
    except Exception as error:
        rows.append(dict(Failure=str(error)))
        qemu.finish(out, "ap-panic-stress", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-panic-stress", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
