"""First real AP: dedicated UEFI profile, never inferred from configured vCPU count."""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("launch_joint", Path(__file__).parents[1] / "commit/firmware.py")
joint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(joint)
cpu, qemu, transition = joint.cpu, joint.qemu, joint.transition


# ====== registers ======

def registers(vm, index):
    vm.command(f"cpu {index}")
    try:
        return cpu.monitor_read(vm, "info registers", lambda t: t if "HLT=" in t and "CR0=" in t and
            re.search(r"\bEFER=[0-9a-fA-F]{16}\b", t) else None)
    finally:
        vm.command("cpu 0")


# ====== value ======

def value(text, name):
    match = re.search(rf"\b{name}=([0-9a-f]+)", text, re.I)
    if not match:
        raise RuntimeError(f"Missing hardware register {name}")
    return int(match[1], 16)


# ====== verify_memory ======

def verify_memory(vm, log, expected_failures, executing):
    root, data, tables = (log.number("KERNEL_AP_TRANSITION_" + x) for x in ("ROOT", "DATA", "TABLES"))
    code = int(transition.low.verify(vm, log, False, expected_failures)["Base"], 16)
    active = vm.physical(root, 512)
    source = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_SOURCE"), 512)
    if any(active[:256]) or any((a & ~0x60) != (b & ~0x60) for a, b in zip(active, source)):
        raise RuntimeError("BSP operational root changed")
    for table in range(4):
        expected = [0] * 512
        if table == 0:
            expected[0] = tables + 4096 | 3; expected[256:] = source[256:]; expected[510] = 0
        elif table < 3:
            expected[0] = tables + (table + 1) * 4096 | 3
        else:
            expected[code >> 12] = code | 1; expected[data >> 12] = data | (1 << 63) | 3
        actual = vm.physical(tables + table * 4096, 512)
        if any((a ^ b) & ~0x60 for a, b in zip(actual, expected)):
            raise RuntimeError("AP transition permissions or extra leaf")
    actual_code, actual_data = vm.physical(code, 512), vm.physical(data, 512)
    if actual_code != cpu.memory(vm, log.number("KERNEL_AP_IMAGE_CODE_EXPECTED"), 512):
        raise RuntimeError("Frozen physical code changed")
    expected_data = cpu.memory(vm, log.number("KERNEL_AP_IMAGE_DATA_EXPECTED"), 512)
    if actual_data[:13] != expected_data[:13] or actual_data[14:136] != expected_data[14:136]:
        raise RuntimeError("Immutable low record/metadata changed")
    arena = cpu.ap_monitor.verify(vm, log, cpu.memory, 4, True, executing)
    transition_owner = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_OWNER"), 19)
    pool = log.number("KERNEL_AP_LOW_POOL")
    live = cpu.memory(vm, pool + 4144, 256)
    pins = cpu.memory(vm, pool + 6192, 32)
    if transition_owner[17:] != [4 | (1 << 32), 1] or cpu.memory(vm, pool, 3)[2] != transition_owner[13] - 4:
        raise RuntimeError("Frozen transition state or physical budget changed")
    for physical, pages, ident in ((data, 1, transition_owner[5]), (tables, 4, transition_owner[9])):
        matches = [i for i in range(64) if live[i * 4:i * 4 + 4] == [physical, pages, ident, pool]]
        if len(matches) != 1 or (pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1:
            raise RuntimeError("Live transition lease lost or no longer pinned")
    first = log.number("KERNEL_AP_CONTEXTS")
    for index in range(1, 4):
        owner = cpu.memory(vm, first + (index - 1) * 448, 56)
        state = 2 if index <= executing else 1
        if owner[0] != first + (index - 1) * 448 or owner[2] != index | (state << 32) or owner[4] != 1 or owner[6] or any(owner[7:]):
            raise RuntimeError("AP owner/state/lock data mismatch")
    return root, actual_data, arena, first


# ====== verify ======

def verify(vm, log, no_nmi, remote_panic=False, sequence=False):
    target = 3 if sequence else 1
    expected_marker = "KERNEL_AP_NO_NMI_QUARANTINED" if no_nmi else "KERNEL_AP_NATIVE_PARKED"
    expected_failures = ["KERNEL_SMP_PANIC"] if remote_panic else []
    if log.count(expected_marker, "OK") != 1 or log.has("KERNEL_READY") or log.failures() != expected_failures:
        raise RuntimeError("Native outcome marker or unexpected failure")
    if log.number("KERNEL_AP_NATIVE_STATE") != 520 | (7 if no_nmi else 6):
        raise RuntimeError("Native Rust state/token mismatch")
    if log.number("KERNEL_AP_NATIVE_CONTEXT_PHASE") != (0 if no_nmi else 2):
        raise RuntimeError("Native context provider phase mismatch")
    if log.number("KERNEL_AP_NATIVE_ERROR") != (5 if no_nmi else 0) or log.number("KERNEL_AP_NATIVE_WAKE_ERROR"):
        raise RuntimeError("Unexpected native transport error")
    if log.number("KERNEL_AP_NATIVE_GATE") != 1 or log.number("KERNEL_AP_NATIVE_NMI") != int(not no_nmi):
        raise RuntimeError("NMI not gated by observed IVT readiness")
    if log.number("KERNEL_AP_NATIVE_INIT") != 1 or log.number("KERNEL_AP_NATIVE_SIPI") not in (1, 2):
        raise RuntimeError("Unexpected INIT/SIPI count")
    deadline = time.monotonic() + 2
    while True:
        text = registers(vm, target)
        if "HLT=1" in text:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError("Target AP did not reach actual HLT")
        vm.command("cont"); time.sleep(0.005); vm.command("stop")
    (vm.directory / f"ap{target}-registers.txt").write_text(text)
    root, actual_data, arena, first = verify_memory(vm, log, expected_failures, 0 if no_nmi else target)
    if actual_data[13] != (1 if no_nmi else 3):
        raise RuntimeError("Low gate/error differs from actual AP progress")
    panic = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 10)
    if panic[:3] != [2, 15, 1 if no_nmi else (1 << (target + 1)) - 1] or panic[3] != int(remote_panic):
        raise RuntimeError("Panic admission/election differs")
    if no_nmi:
        if value(text, "CR0") & 1:
            raise RuntimeError("No-NMI AP left real mode")
    else:
        top = log.number("KERNEL_AP_BASE") + target * 0x40000 + 17 * 4096
        if value(text, "CR3") != root or cpu.gs_base(text) != first + (target - 1) * 448:
            raise RuntimeError("Actual AP root/stack/GS differs")
        if not remote_panic and value(text, "RSP") != top - 1024:
            raise RuntimeError("Parked AP stack differs")
        if value(text, "RFL") & 0x7600 or value(text, "CR0") & 0x80010001 != 0x80010001:
            raise RuntimeError("Actual AP flags, paging or WP differs")
        if value(text, "EFER") & 0xd00 != 0xd00 or value(text, "CR4") & 0x620 != 0x620:
            raise RuntimeError("Actual AP long mode/NX/SSE prerequisites missing")
        frame = cpu.memory(vm, top - 1024, 128)
        if frame[:13] != actual_data[:13] or frame[83] != 6:
            raise RuntimeError("Actual AP private record or PARKED frame differs")
        meta = top + 31 * 4096
        if not re.search(rf"GDT=\s*{meta:016x}\s+00000027", text, re.I) or not re.search(r"(?m)^TR\s*=\s*0018", text, re.I):
            raise RuntimeError("Actual private GDT/TR differs")
        idtr = b"".join(w.to_bytes(8, "little") for w in cpu.memory(vm, log.number("KERNEL_AP_PANIC_IDTR"), 2))
        base = int.from_bytes(idtr[2:10], "little")
        if not re.search(rf"IDT=\s*{base:016x}\s+000001ff", text, re.I):
            raise RuntimeError("Actual AP final IDT differs")
        if remote_panic:
            verify_remote(vm, log, text, root, top, panic)
    if sequence:
        verify_sequence(vm, log, root, first)
    return dict(OsCpusReady=1 if no_nmi else target + 1, StartedAps=target, AdmittedAps=0 if no_nmi else target,
        NoNmi=no_nmi, RemotePanic=remote_panic, Sequence=sequence, Token=520, Root=hex(root), Context=hex(first), Arena=arena)


# ====== verify_sequence ======

def verify_sequence(vm, log, root, first):
    if log.count("KERNEL_AP_GROUP_PARKED", "OK") != 1 or log.count("KERNEL_AP_SEQUENCE_PARKED", "OK") != 2:
        raise RuntimeError("Group outcome markers missing")
    if log.number("KERNEL_AP_GROUP_STATE") != (3 << 9) | (3 << 3) | 6 or log.number("KERNEL_AP_GROUP_ONLINE") != 15:
        raise RuntimeError("Final group token or online mask mismatch")
    verify_parked_cpus(vm, log, root, first)


# ====== verify_parked_cpus ======

def verify_parked_cpus(vm, log, root, first, panic=False, reporter=0, count=4, service=False):
    final_record = vm.physical(log.number("KERNEL_AP_TRANSITION_DATA"), 13)
    for index in range(1, count):
        text = registers(vm, index)
        (vm.directory / f"sequence-cpu-{index}.txt").write_text(text)
        top = log.number("KERNEL_AP_BASE") + index * 0x40000 + 17 * 4096
        frame = cpu.memory(vm, top - 1024, 128)
        expected = final_record[:]
        expected[1] = index | (index << 32)
        expected[2] = (index << 9) | (index << 3)
        expected[7] = top
        expected[8] = top + 31 * 4096
        expected[9] = first + (index - 1) * 448
        if frame[:13] != expected:
            raise RuntimeError("Group private record changed outside its per-CPU fields")
        rsp = value(text, "RSP")
        stack_valid = top + 8 * 4096 <= rsp < top + 12 * 4096 if panic else rsp == top - 1024 - (576 if service else 0)
        terminal_valid = not panic or value(text, "RAX") == (0x50414e4943444f4e if index == reporter else 0x50414e494353544f)
        if "HLT=1" not in text or value(text, "CR3") != root or not stack_valid or not terminal_valid:
            raise RuntimeError("Group CPU not parked on its private stack/root")
        if cpu.gs_base(text) != first + (index - 1) * 448 or (value(text, "RFL") & 0x7600) != (0x200 if service and not panic else 0):
            raise RuntimeError("Group private GS or flags mismatch")
        if frame[0] != 1 | (104 << 32) or frame[1] != index | (index << 32) or frame[2] != (index << 9) | (index << 3):
            raise RuntimeError("Group private record identity/token changed")
        if frame[6] != root or frame[7] != top or frame[9] != first + (index - 1) * 448 or frame[83] != 6:
            raise RuntimeError("Group private record geometry/phase mismatch")
        idtr_name = "KERNEL_AP_RUNTIME_IPI_IDTR" if service else "KERNEL_AP_PANIC_IDTR"
        idtr = b"".join(w.to_bytes(8, "little") for w in cpu.memory(vm, log.number(idtr_name), 2))
        idt = int.from_bytes(idtr[2:10], "little")
        limit = 0xfff if service else 0x1ff
        if not re.search(rf"IDT=\s*{idt:016x}\s+{limit:08x}", text, re.I):
            raise RuntimeError("Group final IDT mismatch")
        if value(text, "CR0") & 0x80010001 != 0x80010001 or value(text, "EFER") & 0xd00 != 0xd00 or value(text, "CR4") & 0x620 != 0x620:
            raise RuntimeError("Group paging/WP/NX/SSE prerequisites missing")
        meta = top + 31 * 4096
        if not re.search(rf"GDT=\s*{meta:016x}\s+00000027", text, re.I) or not re.search(r"(?m)^TR\s*=\s*0018", text, re.I):
            raise RuntimeError("Group private GDT/TR mismatch")


# ====== verify_remote ======

def verify_remote(vm, log, text, root, top, panic):
    state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
    if panic != [2, 15, 3, 1, 2, 2, 0, 2, 0, 2] or state[10:] != [2] + [0] * 69:
        raise RuntimeError("Remote panic owner/targets/ACK/record state mismatch")
    if cpu.memory(vm, log.number("KERNEL_PANIC_OUTPUT"), 1) != [1] or cpu.memory(vm, log.number("KERNEL_PANIC_RECURSION"), 1) != [0]:
        raise RuntimeError("Remote panic report incomplete or recursive")
    for field, expected in (("OWNER", 0), ("TARGETS", 2), ("ACK", 2), ("MISSING", 0), ("OUTCOME", 0), ("RECORD_COMPLETE", 1)):
        if log.number("PANIC_" + field) != expected:
            raise RuntimeError("Serial panic result disagrees with coordinator")
    bsp = registers(vm, 0)
    if "HLT=1" not in bsp or value(bsp, "RAX") != 0x50414e4943444f4e or value(text, "RAX") != 0x50414e494353544f:
        raise RuntimeError("Actual BSP/AP panic terminal marker mismatch")
    if value(bsp, "CR3") != root or value(bsp, "RFL") & 0x200:
        raise RuntimeError("BSP panic root/IF differs")
    (vm.directory / "bsp-panic-registers.txt").write_text(bsp)
    base = top - 17 * 4096
    ist_base, ist_top = base + 25 * 4096, base + 29 * 4096
    if not ist_base <= value(text, "RSP") < ist_top:
        raise RuntimeError("Actual AP stop stack is outside private NMI IST2")
    for index, address in ((0, log.number("KERNEL_PANIC_RECORD")), (1, log.number("KERNEL_AP_PANIC_RECORD"))):
        record = cpu.memory(vm, address, 64)
        if record[:3] != [address, 1, index] or any(record[31:]) or record[7] != root:
            raise RuntimeError("Panic record ownership/reserved/root mismatch")
        if record[11] != 8 or record[14] not in (0, 16) or record[12] & 0x7600 or not record[12] & 2:
            raise RuntimeError("Captured ring/IF/flags differ")
        if record[19] != record[13] or not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
            raise RuntimeError("Captured GPR RSP or RIP differs")
        if index == 0:
            if record[3] != 1 or record[5] != 0x41505f50414e4943 or record[8:10] != [0, 0]:
                raise RuntimeError("BSP voluntary panic reason/kind differs")
        elif record[3] != 3 or record[8:10] != [2, 0] or not ist_base <= record[4] <= ist_top - 56 or not base + 4096 <= record[13] <= top - 1024:
            raise RuntimeError("Remote NMI record or interrupted private stack differs")
    for index in (2, 3):
        address = log.number("KERNEL_AP_BASE") + index * 0x40000 + 48 * 4096 + 256
        record = cpu.memory(vm, address, 64)
        if record[:3] != [address, 1, index] or any(record[3:]):
            raise RuntimeError("Offline AP panic record changed")


# ====== wait_remote ======

def wait_remote(vm):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = vm.log()
        if not vm.alive() or any(item != "KERNEL_SMP_PANIC" for item in log.failures()):
            raise RuntimeError("Unexpected failure before remote panic completion")
        if "KERNEL_SMP_PANIC_HALTED" in log.text:
            text = registers(vm, 0)
            if "HLT=1" in text and value(text, "RAX") == 0x50414e4943444f4e:
                return vm.log()
        time.sleep(0.005)
    raise TimeoutError("Remote panic terminal/serial drain missing")


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--no-nmi", action="store_true")
    parser.add_argument("--remote-panic", action="store_true")
    parser.add_argument("--sequence", action="store_true")
    args = parser.parse_args()
    if args.sequence and (args.remote_panic or args.no_nmi):
        parser.error("Sequence requires positive launch without panic")
    if args.remote_panic and args.no_nmi:
        parser.error("Remote panic requires the positive AP launch")
    out = transition.ROOT / "kernel/build" / ("first-ap-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    rows = []
    try:
        for index in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{index}", args.image, "std", smp=4) as vm:
                try:
                    log = wait_remote(vm) if args.remote_panic else joint.wait_terminal(vm, False)
                    vm.command("stop")
                    rows.append(verify(vm, log, args.no_nmi, args.remote_panic, args.sequence))
                except BaseException:
                    vm.command("stop")
                    for target in range(4):
                        (vm.directory / f"failure-cpu-{target}.txt").write_text(registers(vm, target))
                    raise
                print(f"PASS: native AP {index}/{args.boots}; admitted={3 if args.sequence else int(not args.no_nmi)}", flush=True)
    except Exception as error:
        rows.append(dict(Failure=str(error)))
        qemu.finish(out, "first-ap", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "first-ap", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
