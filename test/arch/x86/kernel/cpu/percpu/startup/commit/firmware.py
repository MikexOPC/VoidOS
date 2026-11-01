"""Joint resource seal and expected fail-stop; no AP startup."""
import argparse
import importlib.util
import time
import uuid
from pathlib import Path

spec = importlib.util.spec_from_file_location("joint_transition", Path(__file__).parents[2] / "transition/firmware.py")
transition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transition)
cpu, qemu = transition.cpu, transition.qemu


# ====== wait_terminal ======

def wait_terminal(vm, failure):
    deadline = time.monotonic() + 90
    expected = ["AP_JOINT_PRIVATE_SEAL_FAILED"] if failure else []
    target = 0x4b50414e49430001 if failure else 0x5452414e53505245
    while time.monotonic() < deadline:
        log = vm.log()
        if not vm.alive() or any(item not in expected for item in log.failures()):
            raise RuntimeError("Unexpected joint failure: " + str(log.failures()))
        marker = log.has("AP_JOINT_PRIVATE_SEAL_FAILED", "FAIL") if failure else log.has("KERNEL_AP_TRANSITION_TEST_READY", "OK")
        if marker:
            rax, _, halted = transition.registers(vm)
            if halted and rax == target:
                return log
        time.sleep(0.005)
    raise TimeoutError("Joint terminal checkpoint missing")


# ====== verify ======

def verify(vm, log, case):
    failure = case in ("before", "after")
    details = transition.verify(vm, log, False, image=True, joint=True, failure=failure)
    group_address = log.number("KERNEL_AP_JOINT_OWNER")
    resources = log.number("KERNEL_AP_JOINT_RESOURCES")
    for address in (group_address, resources):
        if address & 7 or not 0xffffffff80000000 <= address <= 0xffffffff80400000 - 4096:
            raise RuntimeError("Joint owner outside kernel image")
    group = cpu.memory(vm, group_address, 9)
    image = log.number("KERNEL_AP_IMAGE_OWNER")
    if group[:2] != [group_address, image] or group[2:4] != [resources, resources] or group[4] & 0xffffffff != 4:
        raise RuntimeError("Joint group identity, operations or count mismatch")
    resource_bytes = group[4] >> 32
    if resource_bytes != 192:
        raise RuntimeError("AP resource owner ABI changed")
    if group[8] != (3 | (2 << 32) if failure else 2):
        raise RuntimeError("Joint phase or failure code mismatch")
    if log.count("KERNEL_AP_JOINT_SEALED", "OK") != int(not failure) or log.count("KERNEL_AP_JOINT_RETENTION_GUARDS_OK", "OK") != 1:
        raise RuntimeError("Missing retention guard proof or incorrect success publication")
    private = cpu.memory(vm, resources, 24)
    expected_committed = 0 if case == "before" else 1
    if private[13] != resources or private[23] & 0xffffffff != expected_committed:
        raise RuntimeError("Partial/private commit state mismatch")
    if private[22] != (1 | (1 << 32)) or private[23] >> 32:
        raise RuntimeError("AP publication/registration/padding changed")
    arena = cpu.ap_monitor.verify(vm, log, cpu.memory, 4, True)
    contexts = cpu.ap_preparation.verify(vm, log, cpu.memory, 4, True, False)
    if case == "panic":
        if log.count("KERNEL_AP_JOINT_PANIC_PREPARED", "OK") != 1 or log.count("KERNEL_PANIC_BACKEND_PREPARED", "OK") != 1:
            raise RuntimeError("Panic was not prepared after joint seal")
        if not log.before("KERNEL_AP_JOINT_SEALED", "KERNEL_PANIC_BACKEND_PREPARED"):
            raise RuntimeError("Panic initialization preceded resource seal")
        state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
        if state[:3] != [2, 15, 1] or state[3] or state[7] or state[9]:
            raise RuntimeError("Panic is not prepared with BSP alone admitted")
        record_address = log.number("KERNEL_AP_PANIC_RECORD")
        ap_base = log.number("KERNEL_AP_BASE") + 0x40000
        if record_address != ap_base + 48 * 4096 + 256:
            raise RuntimeError("AP panic record outside private metadata")
        record = cpu.memory(vm, record_address, 64)
        if record[:3] != [record_address, 1, 1] or any(record[3:]):
            raise RuntimeError("Prepared AP panic record differs")
        idtr_address = log.number("KERNEL_AP_PANIC_IDTR")
        words = cpu.memory(vm, idtr_address, 2)
        raw = b"".join(value.to_bytes(8, "little") for value in words)
        idt_address = int.from_bytes(raw[2:10], "little")
        if int.from_bytes(raw[:2], "little") != 511 or idt_address & 15:
            raise RuntimeError("AP panic IDTR malformed")
        gates = b"".join(value.to_bytes(8, "little") for value in cpu.memory(vm, idt_address, 64))
        for vector in range(32):
            gate = gates[vector * 16:(vector + 1) * 16]
            if gate[2:6] != bytes([8, 0, 2 if vector == 2 else 3 if vector == 8 else 1, 0x8e]):
                raise RuntimeError("Prepared panic IST/selector/type differs")
        if log.count("KERNEL_AP_ADMISSION_REJECTIONS_OK", "OK") != 1 or log.count("KERNEL_AP_ADMISSION_GROUP_PUBLISHED", "OK") != 1 or not log.before(
                "KERNEL_AP_JOINT_PANIC_PREPARED", "KERNEL_AP_ADMISSION_GROUP_PUBLISHED"):
            raise RuntimeError("Admission publication missing or precedes panic preparation")
        admission = log.number("KERNEL_AP_ADMISSION_GROUP")
        if admission & 7 or not 0xffffffff80000000 <= admission <= 0xffffffff80400000 - 5160:
            raise RuntimeError("Admission descriptor outside kernel image")
        snapshot = cpu.memory(vm, admission, 645)
        if snapshot[0] != 1 | (5160 << 32) or snapshot[1] & 0xffffffff != 4 or snapshot[2] != admission:
            raise RuntimeError("Admission header ABI/count/self differs")
        if snapshot[1] >> 32 != private[11] & 0xffffffff or snapshot[3:5] != private[1:3]:
            raise RuntimeError("Admission root/PAT/physical width differs")
        first = log.number("KERNEL_AP_CONTEXTS")
        bsp_wait = log.number("KERNEL_PERCPU_WAIT")
        bsp_local = first - 448
        identity = cpu.memory(vm, log.number("KERNEL_CPU_IDENTITIES") + 32, 8)
        for index in range(4):
            record = snapshot[5 + index * 10:5 + (index + 1) * 10]
            local, wait = bsp_local + index * 448, bsp_wait + index * 64
            actual_wait = cpu.memory(vm, wait, 7)
            expected_id = identity[index * 2] >> 32
            if record != [index | (expected_id << 32), local, wait, *actual_wait]:
                raise RuntimeError("Admission CPU identity/context/wait snapshot differs")
        if any(snapshot[45:]):
            raise RuntimeError("Unused admission slots not zero")
        if log.count("KERNEL_AP_CONTEXT_ACCESS_CLOSED", "OK") != 1 or not log.before(
                "KERNEL_AP_ADMISSION_GROUP_PUBLISHED", "KERNEL_AP_CONTEXT_ACCESS_CLOSED"):
            raise RuntimeError("Prepared-context access not closed after descriptor publication")
        if log.count("KERNEL_AP_MUTABLE_PROBES_BLOCKED", "OK") != 1:
            raise RuntimeError("Mutable AP arena probes remained reachable after access closure")
    if log.has("KERNEL_READY") or log.has("KERNEL_EXCEPTION"):
        raise RuntimeError("Joint diagnostic continued boot or faulted unexpectedly")
    return dict(Case=case, Group=hex(group_address), State=3 if failure else 2,
        PrivateCommitted=bool(expected_committed), ApStarted=0, Transition=details, Arena=arena, Contexts=contexts)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--case", choices=("positive", "before", "after", "panic"), default="positive")
    args = parser.parse_args()
    out = transition.ROOT / "kernel/build" / ("ap-joint-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    rows = []
    try:
        for index in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{index}", args.image, "std", smp=4) as vm:
                log = wait_terminal(vm, args.case in ("before", "after"))
                vm.command("stop")
                rows.append(verify(vm, log, args.case))
                print(f"PASS: joint {args.case} {index}/{args.boots}; physical resources retained, no AP", flush=True)
    except Exception as error:
        rows.append(dict(Failure=str(error)))
        qemu.finish(out, "ap-joint", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-joint", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
