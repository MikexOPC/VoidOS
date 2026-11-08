"""Verify real CPL3, expected faults, five controlled NMI entries and timer IRQs from CPL3."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

root = Path(__file__).resolve().parents[6]
spec = importlib.util.spec_from_file_location("k5_qemu", root / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)

REGISTERS = re.compile(rb"RIP=([0-9a-fA-F]+)[^\r\n]*\bCPL=([0-3])\b")
HALT = re.compile(rb"RAX=000000004b354641[^\r\n]*\r?\n(?:[^\r\n]*\r?\n){0,4}RIP=[^\r\n]*\bHLT=1\b", re.I)
USER_FAULT = re.compile(rb"\bv=(06|0d|0e) e=([0-9a-f]+) i=0 cpl=3\b", re.I)
AP_IRQ = re.compile(rb"\bv=d1 e=[0-9a-f]+ i=0 cpl=3\b", re.I)
USER_IRQ = re.compile(rb"\bv=41 e=[0-9a-f]+ i=0 cpl=3\b", re.I)
RSP0_FRAME = 0x409000 - 40
KERNEL_PF = re.compile(rb"\bv=0e e=([0-9a-f]+) i=0 cpl=0\b", re.I)
SYS_TF = re.compile(rb"\bv=01 e=[0-9a-f]+ i=0 cpl=0\b", re.I)
USER_TF = re.compile(rb"\bv=01 e=[0-9a-f]+ i=0 cpl=3\b", re.I)
AP_REASON = {6: 0x4b35415046414900, 7: 0x4b35415046414900, 8: 0x4b354150544d4f00, 9: 0x4b34544c4241434b}


# ====== wait_window ======

def wait_window(vm, start, end, cpl):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline and vm.alive():
        offset = vm.monitor.stat().st_size
        vm.command("info registers")
        poll = min(deadline, time.monotonic() + 0.3)
        while time.monotonic() < poll:
            with vm.monitor.open("rb") as monitor:
                monitor.seek(offset)
                match = REGISTERS.search(monitor.read())
            if match:
                rip = int(match[1], 16)
                if start <= rip < end and int(match[2]) == cpl:
                    return
                break
            time.sleep(0.005)
    raise RuntimeError(f"Missing diagnostic window CPL={cpl}, RIP={start:x}..{end:x}")


# ====== boot ======

def boot(directory, image):
    with qemu.Vm(directory, image, smp=1) as vm:
        for index, cpl in enumerate((0, 0, 3, 0, 3), 1):
            log = vm.wait("KERNEL_K5_NMI_ARMED", occurrences=index, level="OK")
            first, last = (log.number(name, index - 1) for name in ("K5_NMI_FIRST", "K5_NMI_LAST"))
            wait_window(vm, first, last, cpl)
            vm.command("nmi")
            log = vm.wait("KERNEL_K5_NMI_RESUMED", occurrences=index, level="OK")
            if not first <= log.number("K5_NMI_RIP", index - 1) < last:
                raise RuntimeError("NMI interrupted the wrong instruction window")
        log = vm.wait("KERNEL_READY", level="OK")
        if log.failures() or log.terminal() or not log.before("KERNEL_K5_RING3_OK", "KERNEL_READY"):
            raise RuntimeError("Missing clean kernel continuation")
        if not log.before("KERNEL_K5_IRQ_OK", "KERNEL_READY") or not log.has("KERNEL_K5_IRQ_RESUMED"):
            raise RuntimeError("Missing CPL3 IRQ continuation")
        if log.numbers("K5_FAULT_VECTOR") != [13, 14, 14, 6] or log.numbers("K5_FAULT_ERROR") != [0, 5, 21, 0]:
            raise RuntimeError("Unexpected fault sequence")
        if log.numbers("K5_USER_CS") != [0x33] * 4:
            raise RuntimeError("Faults did not come from ring 3")
        if log.numbers("K5_IRQ_CS") != [0x33] * 2 or log.numbers("K5_IRQ_STACK") != [RSP0_FRAME] * 2:
            raise RuntimeError("IRQ did not enter from ring 3 on the RSP0 stack")
        gs = log.numbers("K5_NMI_GS")
        if len(gs) != 5 or gs[0] < 0xffff800000000000 or gs[1:3] != [0, 0] or gs[3] != gs[0] or gs[4] != 0:
            raise RuntimeError("Unexpected GS phases")
        trace = vm.trace.read_bytes()
        if [(int(vector, 16), int(error, 16)) for vector, error in USER_FAULT.findall(trace)] != [
            (13, 0), (14, 5), (14, 21), (6, 0), (6, 0), (6, 0),
        ]:
            raise RuntimeError("QEMU did not observe the same CPL3 faults")
        if len(USER_IRQ.findall(trace)) != 4:
            raise RuntimeError("QEMU did not observe four timer IRQs from CPL3")
        if b"triple fault" in trace.lower():
            raise RuntimeError("Unexpected triple fault")
        return dict(Faults=6, Nmis=5, UserIrqs=4, Cleanup=True, UserCpl=3)


# ====== sys_boot ======

def sys_boot(directory, image):
    with qemu.Vm(directory, image, smp=1) as vm:
        for index, cpl in enumerate((0, 3), 1):
            log = vm.wait("KERNEL_K5_NMI_ARMED", occurrences=index, level="OK")
            first, last = (log.number(name, index - 1) for name in ("K5_NMI_FIRST", "K5_NMI_LAST"))
            wait_window(vm, first, last, cpl)
            vm.command("nmi")
            vm.wait("KERNEL_K5_NMI_RESUMED", occurrences=index, level="OK")
        log = vm.wait("KERNEL_READY", level="OK")
        if log.failures() or log.terminal() or not log.before("KERNEL_K5_SYSCALL_OK", "KERNEL_READY"):
            raise RuntimeError("Missing clean syscall continuation")
        if not log.has("KERNEL_K5_SCE_OFF_UD") or log.numbers("K5_SYS_CALLS") != [3, 3]:
            raise RuntimeError("Missing syscall evidence")
        if log.numbers("K5_NMI_GS") != [0, 0]:
            raise RuntimeError("Unexpected GS at the syscall NMI windows")
        trace = vm.trace.read_bytes()
        if len(USER_IRQ.findall(trace)) != 2 or USER_FAULT.findall(trace) != [(b"06", b"0000")] * 3:
            raise RuntimeError("Unexpected CPL3 events")
        if SYS_TF.search(trace) or b"triple fault" in trace.lower():
            raise RuntimeError("Unexpected kernel debug or triple fault")
        return dict(Calls=6, Nmis=2, UserIrqs=2, SceOff=True)


# ====== sys_negative ======

def sys_negative(directory, image, case):
    with qemu.Vm(directory, image, smp=1) as vm:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and vm.alive():
            log = vm.log()
            if log.has("KERNEL_K5_SYSCALL_OK") or log.has("KERNEL_READY"):
                raise RuntimeError("Negative syscall case was accepted")
            vm.command("info registers")
            time.sleep(0.1)
            halted = HALT.search(vm.monitor.read_bytes())
            if (case == 17 and log.terminal()) or (case != 17 and halted):
                trace = vm.trace.read_bytes()
                if b"triple fault" in trace.lower() or SYS_TF.search(trace):
                    raise RuntimeError("Unexpected kernel fault in the negative case")
                if case == 15 and not USER_TF.search(trace):
                    raise RuntimeError("Missing CPL3 debug trap after the return")
                if case in (10, 11, 12, 13, 18) and USER_FAULT.findall(trace):
                    raise RuntimeError("Invalid frame reached ring 3")
                return dict(ExpectedRejection=case)
    raise RuntimeError("Negative syscall case was not rejected")


# ====== copy_boot ======

def copy_boot(directory, image):
    with qemu.Vm(directory, image, smp=1) as vm:
        log = vm.wait("KERNEL_K5_NMI_ARMED", level="OK")
        wait_window(vm, log.number("K5_NMI_FIRST"), log.number("K5_NMI_LAST"), 0)
        vm.command("nmi")
        log = vm.wait("KERNEL_READY", level="OK")
        if log.failures() or log.terminal() or not log.before("KERNEL_K5_COPY_OK", "KERNEL_READY"):
            raise RuntimeError("Missing clean copy continuation")
        if not log.has("KERNEL_K5_SMEP_SMAP_SUPPORTED") or log.numbers("K5_COPY_FAULTS") != [4]:
            raise RuntimeError("Missing SMAP or fault evidence")
        trace = vm.trace.read_bytes()
        faults = [int(item, 16) for item in KERNEL_PF.findall(trace)][-4:]
        if faults != [0, 3, 0, 3] or USER_FAULT.findall(trace) != [(b"06", b"0000")]:
            raise RuntimeError(f"Unexpected fault sequence {faults}")
        if b"triple fault" in trace.lower():
            raise RuntimeError("Unexpected triple fault")
        return dict(RecoveredFaults=4, Nmis=1, Smap=True, Smep=True)


# ====== copy_negative ======

def copy_negative(directory, image, case):
    expected = {20: 0x3, 21: 0x11, 22: 0x0, 23: 0x0, 24: 0x2, 25: None}[case]
    with qemu.Vm(directory, image, smp=1) as vm:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and vm.alive():
            log = vm.log()
            if log.has("KERNEL_K5_COPY_OK") or log.has("KERNEL_READY"):
                raise RuntimeError("Negative copy case was accepted")
            vm.command("info registers")
            time.sleep(0.1)
            if HALT.search(vm.monitor.read_bytes()):
                trace = vm.trace.read_bytes()
                faults = [int(item, 16) for item in KERNEL_PF.findall(trace)]
                if b"triple fault" in trace.lower():
                    raise RuntimeError("Negative copy case caused a triple fault")
                if expected is None and faults or expected is not None and (not faults or faults[-1] != expected):
                    raise RuntimeError(f"Unexpected kernel faults {faults}")
                return dict(ExpectedRejection=case, Faults=faults[-1:])
    raise RuntimeError("Negative copy case was not rejected")


# ====== ap_boot ======

def ap_boot(directory, image):
    with qemu.Vm(directory, image, smp=3) as vm:
        log = vm.wait("KERNEL_K5_AP_OK", level="OK", timeout=120)
        if log.failures() or log.terminal():
            raise RuntimeError("Unexpected failure in the AP profile")
        if log.numbers("K5_AP_CPU") != [1, 2]:
            raise RuntimeError("Unexpected AP sequence")
        gs, tss = log.numbers("K5_AP_GS"), log.numbers("K5_AP_TSS")
        if len(set(gs)) != 2 or tss[1] - tss[0] != 0x40000:
            raise RuntimeError("AP probes did not use per-CPU GS and TSS")
        if log.numbers("K5_IRQ_CS") != [0x33] * 2 or log.numbers("K5_IRQ_STACK") != [RSP0_FRAME] * 2:
            raise RuntimeError("AP IRQ did not enter from ring 3 on RSP0")
        trace = vm.trace.read_bytes()
        if len(AP_IRQ.findall(trace)) != 8 or b"triple fault" in trace.lower():
            raise RuntimeError("QEMU did not observe eight AP IRQs from CPL3")
        return dict(Aps=2, UserIrqs=8, Cleanup=True)


# ====== ap_negative ======

def ap_negative(directory, image, case):
    with qemu.Vm(directory, image, smp=3) as vm:
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline and vm.alive():
            log = vm.log()
            if log.has("KERNEL_K5_AP_OK"):
                raise RuntimeError("Negative AP case was accepted")
            if "KERNEL_SMP_PANIC_HALTED" in log.text:
                irqs = len(AP_IRQ.findall(vm.trace.read_bytes()))
                reason = log.number("PANIC_REASON_ADDRESS")
                if reason != AP_REASON[case] or (irqs != 0 if case <= 7 else irqs == 0):
                    raise RuntimeError(f"Unexpected AP rejection: reason {reason:x}, IRQs {irqs}")
                return dict(ExpectedRejection=case, Reason=hex(reason), Irqs=irqs)
            time.sleep(0.1)
    raise RuntimeError("Negative AP case was not rejected")


# ====== irq_negative ======

def irq_negative(directory, image, case):
    with qemu.Vm(directory, image, smp=1) as vm:
        for index, cpl in enumerate((0, 0, 3), 1):
            log = vm.wait("KERNEL_K5_NMI_ARMED", occurrences=index, level="OK")
            first, last = (log.number(name, index - 1) for name in ("K5_NMI_FIRST", "K5_NMI_LAST"))
            wait_window(vm, first, last, cpl)
            vm.command("nmi")
            vm.wait("KERNEL_K5_NMI_RESUMED", occurrences=index, level="OK")
        log = vm.wait("KERNEL_K5_NMI_ARMED", occurrences=4, level="OK")
        if case <= 3:
            wait_window(vm, log.number("K5_NMI_FIRST", 3), log.number("K5_NMI_LAST", 3), 0)
            vm.command("nmi")
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline and vm.alive():
            log = vm.log()
            if log.has("KERNEL_K5_IRQ_RESUMED") or log.has("KERNEL_K5_IRQ_OK") or log.has("KERNEL_READY"):
                raise RuntimeError("Negative IRQ case was accepted")
            vm.command("info registers")
            time.sleep(0.1)
            halted = HALT.search(vm.monitor.read_bytes())
            if (case in (3, 5) and halted) or (case not in (3, 5) and log.terminal()):
                trace = vm.trace.read_bytes()
                if b"triple fault" in trace.lower():
                    raise RuntimeError("Negative IRQ case caused a triple fault")
                if len(USER_IRQ.findall(trace)) != {1: 3, 2: 3, 3: 1, 4: 0, 5: 0}[case]:
                    raise RuntimeError("Unexpected CPL3 IRQ count")
                return dict(ExpectedRejection=case)
    raise RuntimeError("Negative IRQ case was not rejected")


# ====== missing_nmi ======

def missing_nmi(directory, image):
    with qemu.Vm(directory, image, smp=1) as vm:
        vm.wait("KERNEL_K5_NMI_ARMED", level="OK")
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline and vm.alive():
            vm.command("info registers")
            time.sleep(0.1)
            if HALT.search(vm.monitor.read_bytes()):
                log = vm.log()
                if log.has("KERNEL_K5_RING3_OK") or log.has("KERNEL_READY") or log.has("KERNEL_K5_NMI_RESUMED"):
                    raise RuntimeError("Missing NMI allowed the boot to continue")
                if b"triple fault" in vm.trace.read_bytes().lower():
                    raise RuntimeError("Missing NMI caused a triple fault")
                return dict(ExpectedStop="missing NMI", HaltMarker="4b354641")
    raise RuntimeError("Missing NMI did not reach its diagnostic halt")


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--boots", default=10, type=int, choices=range(1, 11))
    parser.add_argument("--missing-nmi", action="store_true")
    parser.add_argument("--irq-negative", type=int, choices=range(1, 6))
    parser.add_argument("--ap", action="store_true")
    parser.add_argument("--sys", action="store_true")
    parser.add_argument("--copy", action="store_true")
    parser.add_argument("--copy-negative", type=int, choices=range(20, 26))
    parser.add_argument("--sys-negative", type=int, choices=range(10, 19))
    parser.add_argument("--ap-negative", type=int, choices=range(6, 10))
    args = parser.parse_args()
    out = root / "kernel/build" / ("k5-privilege-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    scenario = missing_nmi if args.missing_nmi else boot
    if args.copy:
        scenario = copy_boot
    if args.copy_negative:
        scenario = lambda directory, image: copy_negative(directory, image, args.copy_negative)
    if args.sys:
        scenario = sys_boot
    if args.sys_negative:
        scenario = lambda directory, image: sys_negative(directory, image, args.sys_negative)
    if args.ap:
        scenario = ap_boot
    if args.ap_negative:
        scenario = lambda directory, image: ap_negative(directory, image, args.ap_negative)
    if args.irq_negative:
        scenario = lambda directory, image: irq_negative(directory, image, args.irq_negative)
    for index in range(args.boots):
        try:
            results.append(scenario(out / f"boot-{index + 1}", args.image.resolve()))
        except (OSError, RuntimeError) as error:
            results.append(dict(Failure=str(error)))
            break
    passed = sum("Failure" not in result for result in results)
    return 0 if qemu.finish(out, "k5-privilege", args.image, results, passed, args.boots) else 1


if __name__ == "__main__":
    raise SystemExit(main())
