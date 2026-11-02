"""Real four-core coordinated panic: BSP or AP3 origin, private records and NMI stacks."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("group_launch", Path(__file__).parents[2] / "startup/transport/firmware.py")
launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launch)
cpu, qemu, transition = launch.cpu, launch.qemu, launch.transition
DONE, STOP = 0x50414e4943444f4e, 0x50414e494353544f


# ====== wait ======

def wait(vm, origin):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = vm.log()
        if not vm.alive() or any(x != "KERNEL_SMP_PANIC" for x in log.failures()):
            raise RuntimeError("Unexpected failure before group panic")
        if "KERNEL_SMP_PANIC_HALTED" in log.text:
            if log.number("PANIC_OWNER") != origin:
                raise RuntimeError("Wrong panic origin; fallback cannot count as AP proof")
            text = launch.registers(vm, origin)
            if "HLT=1" in text and launch.value(text, "RAX") == DONE:
                return vm.log()
        time.sleep(0.005)
    raise TimeoutError("Group panic terminal or serial drain missing")


# ====== settle ======

def settle(vm, origin, count=4):
    if not 1 <= count <= 64 or not 0 <= origin < count:
        raise ValueError("Invalid panic group size or origin")
    deadline = time.monotonic() + 2
    while True:
        texts = [launch.registers(vm, i) for i in range(count)]
        if all("HLT=1" in t and launch.value(t, "RAX") == (DONE if i == origin else STOP)
               for i, t in enumerate(texts)):
            return texts
        if time.monotonic() >= deadline:
            raise RuntimeError("A group member did not reach its panic HLT marker")
        vm.command("cont")
        time.sleep(0.005)
        vm.command("stop")


# ====== idt ======

def idt(vm, address):
    raw = b"".join(w.to_bytes(8, "little") for w in cpu.memory(vm, address, 2))
    return int.from_bytes(raw[2:10], "little"), int.from_bytes(raw[:2], "little")


# ====== verify_cpu ======

def verify_cpu(vm, log, index, text, root, first, final_record, origin):
    value = lambda name: launch.value(text, name)
    if value("CR3") != root or value("RFL") & 0x7600 or value("CR0") & 0x80010001 != 0x80010001:
        raise RuntimeError("Actual CPU root/IF/WP/paging differs")
    if value("EFER") & 0xd00 != 0xd00 or value("CR4") & 0x620 != 0x620:
        raise RuntimeError("Actual CPU long mode/NX/SSE prerequisites differ")
    if index:
        base = log.number("KERNEL_AP_BASE") + index * 0x40000
        top, meta = base + 17 * 4096, base + 48 * 4096
        main_base, main_top = base + 4096, top - 1024
        ist_base, ist_top = base + 25 * 4096, base + 29 * 4096
        address = meta + 256
        frame = cpu.memory(vm, top - 1024, 128)
        expected = final_record[:13]
        expected[1], expected[2] = index | index << 32, index << 9 | index << 3
        expected[7:10] = [top, meta, first + (index - 1) * 448]
        if frame[:13] != expected or frame[83] != 6:
            raise RuntimeError("Private AP record or parked phase changed")
        if cpu.gs_base(text) != first + (index - 1) * 448:
            raise RuntimeError("Actual private AP GS differs")
        if not re.search(rf"GDT=\s*{meta:016x}\s+00000027", text, re.I) or not re.search(r"(?m)^TR\s*=\s*0018", text, re.I):
            raise RuntimeError("Actual private AP GDT/TR differs")
        idtr = log.number("KERNEL_AP_PANIC_IDTR")
    else:
        main_base, main_top = (log.number("KERNEL_GROUP_BSP_MAIN_" + x) for x in ("BASE", "TOP"))
        ist_top = log.number("KERNEL_IST2_TOP")
        ist_base = ist_top - 16384
        address = log.number("KERNEL_PANIC_RECORD")
        if address & 63 or not 0xffffffff80000000 <= address <= 0xffffffff80400000 - 512:
            raise RuntimeError("BSP panic record outside aligned image storage")
        idtr = log.number("KERNEL_GROUP_BSP_IDTR")
    idt_base, limit = idt(vm, idtr)
    if not re.search(rf"IDT=\s*{idt_base:016x}\s+{limit:08x}", text, re.I):
        raise RuntimeError("Actual final IDT differs")
    record = cpu.memory(vm, address, 64)
    if record[:3] != [address, 1, index] or record[7] != root or any(record[31:]):
        raise RuntimeError("Captured record ownership/root/reserved differs")
    if record[11] != 8 or record[14] not in (0, 16) or record[12] & 0x7600 or not record[12] & 2:
        raise RuntimeError("Captured ring/flags differ")
    if record[19] != record[13] or not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
        raise RuntimeError("Captured GPR RSP/RIP differs")
    if index == origin == 0:
        if record[3] != 1 or record[5] != 0x4752504253500001 or record[8:10] != [0, 0]:
            raise RuntimeError("BSP voluntary origin differs")
        if not main_base <= record[4] <= main_top - 136 or record[13] != record[4] + 136:
            raise RuntimeError("Voluntary raw frame outside BSP main stack")
        if not main_base <= value("RSP") < main_top:
            raise RuntimeError("Actual BSP panic stack differs")
    else:
        if record[3] != (2 if index == origin else 3) or record[5] or record[8:10] != [2, 0]:
            raise RuntimeError("Origin/follower NMI classification differs")
        if not ist_base <= record[4] <= ist_top - 56 or not ist_base <= value("RSP") < ist_top:
            raise RuntimeError("NMI frame or terminal stack outside private IST2")
        if not main_base <= record[13] <= main_top:
            raise RuntimeError("Interrupted RSP outside own main stack")


# ====== verify ======

def verify(vm, log, origin):
    if log.failures() != ["KERNEL_SMP_PANIC"] or log.has("KERNEL_READY") or log.has("KERNEL_AP_TRANSITION_TEST_READY"):
        raise RuntimeError("Unexpected group outcome")
    if log.count("KERNEL_AP_GROUP_PARKED", "OK") != 1 or log.count("KERNEL_AP_SEQUENCE_PARKED", "OK") != 2:
        raise RuntimeError("Full group was not parked before panic")
    if log.number("KERNEL_AP_GROUP_STATE") != 1566 or log.number("KERNEL_AP_GROUP_ONLINE") != 15 or log.number("KERNEL_GROUP_PANIC_ORIGIN") != origin:
        raise RuntimeError("Group progress or requested origin differs")
    root, final_record, arena, first = launch.verify_memory(vm, log, ["KERNEL_SMP_PANIC"], 3)
    if final_record[13] != 3:
        raise RuntimeError("Final low gate/error differs")
    targets = 15 & ~(1 << origin)
    state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
    if state[:9] != [2, 15, 15, origin + 1, targets, targets, 0, 2, 0] or state[9:13] != [2] * 4 or any(state[13:]):
        raise RuntimeError("Coordinator owner/targets/ACK/record states differ")
    for name, expected in (("OUTPUT", 1), ("RECURSION", 0)):
        if cpu.memory(vm, log.number("KERNEL_PANIC_" + name), 1) != [expected]:
            raise RuntimeError("Report incomplete or recursive")
    for name, expected in (("OWNER", origin), ("TARGETS", targets), ("ACK", targets), ("MISSING", 0),
                           ("OUTCOME", 0), ("RECORD_COMPLETE", 1), ("KIND", 1 if origin == 0 else 2),
                           ("VECTOR", 0 if origin == 0 else 2), ("ERROR", 0)):
        if log.number("PANIC_" + name) != expected:
            raise RuntimeError(f"Serial {name} disagrees with panic state")
    for index, text in enumerate(settle(vm, origin)):
        (vm.directory / f"cpu-{index}-registers.txt").write_text(text, encoding="utf-8")
        verify_cpu(vm, log, index, text, root, first, final_record, origin)
    return dict(ConfiguredVcpus=4, OsCpusReady=4, PanicOrigin=origin, Targets=targets, Ack=targets,
                Root=hex(root), Arena=arena)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--origin", choices=("bsp", "ap"), required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 11))
    args = parser.parse_args()
    origin = 0 if args.origin == "bsp" else 3
    out = transition.ROOT / "kernel/build" / ("group-panic-" + uuid.uuid4().hex)
    out.mkdir()
    rows = []
    try:
        for iteration in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{iteration}", args.image, "std", smp=4) as vm:
                try:
                    log = wait(vm, origin)
                    vm.command("stop")
                    rows.append(verify(vm, log, origin))
                except BaseException:
                    vm.command("stop")
                    for index in range(4):
                        (vm.directory / f"failure-cpu-{index}.txt").write_text(launch.registers(vm, index), encoding="utf-8")
                    raise
            print(f"PASS: group panic {args.origin} {iteration}/{args.boots}; four real cores, all ACK", flush=True)
    except Exception as error:
        rows.append(dict(Failure=str(error)))
        qemu.finish(out, "group-panic", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "group-panic", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
