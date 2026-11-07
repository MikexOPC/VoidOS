"""Verify real CPL3, expected faults, and three controlled NMI entries."""
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
        for index, cpl in enumerate((0, 0, 3), 1):
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
        if log.numbers("K5_FAULT_VECTOR") != [13, 14, 14, 6] or log.numbers("K5_FAULT_ERROR") != [0, 5, 21, 0]:
            raise RuntimeError("Unexpected fault sequence")
        if log.numbers("K5_USER_CS") != [0x33] * 4:
            raise RuntimeError("Faults did not come from ring 3")
        gs = log.numbers("K5_NMI_GS")
        if len(gs) != 3 or gs[0] < 0xffff800000000000 or gs[1:] != [0, 0]:
            raise RuntimeError("Unexpected GS phases")
        trace = vm.trace.read_bytes()
        if [(int(vector, 16), int(error, 16)) for vector, error in USER_FAULT.findall(trace)] != [
            (13, 0), (14, 5), (14, 21), (6, 0),
        ]:
            raise RuntimeError("QEMU did not observe the same CPL3 faults")
        if b"triple fault" in trace.lower():
            raise RuntimeError("Unexpected triple fault")
        return dict(Faults=4, Nmis=3, Cleanup=True, UserCpl=3)


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
    args = parser.parse_args()
    out = root / "kernel/build" / ("k5-privilege-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    scenario = missing_nmi if args.missing_nmi else boot
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
