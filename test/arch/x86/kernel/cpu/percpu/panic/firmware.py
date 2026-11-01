"""K3-F BSP hardware panic tests; configured APs remain stopped."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path
ROOT = Path(__file__).resolve().parents[7]
spec = importlib.util.spec_from_file_location("panic_parent", Path(__file__).parent.parent / "firmware.py")
parent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parent)
qemu = parent.qemu
MARKERS = {"prepare": 0x50414e4943505245, "prepare-rollback": 0x50414e4943505245, "recursive": 0x50414e4943524543}

# ====== registers ======

def registers(vm):
    def ready(text):
        rax = re.search(r"\bRAX=([0-9a-fA-F]+)", text)
        cr0 = re.search(r"\bCR0=([0-9a-fA-F]+)", text)
        if rax and cr0 and "HLT=" in text:
            return dict(Rax=int(rax[1], 16), Cr0=int(cr0[1], 16), Halted="HLT=1" in text)
        return None
    return parent.monitor_read(vm, "info registers", ready)

# ====== boot ======

def boot(image, out, iteration, case, smp):
    with qemu.Vm(out / f"boot-{iteration}", image, "std", smp=smp) as vm:
        deadline = time.monotonic() + 90
        terminal = MARKERS.get(case, 0x50414e4943444f4e)
        while time.monotonic() < deadline:
            log = vm.log()
            if not vm.alive(): raise RuntimeError("Owned VM exited before panic proof")
            if log.has("KERNEL_EXCEPTION") or log.has("PANIC_PREPARE_FAILED"):
                raise RuntimeError("Legacy exception or prepare failure")
            if log.has("KERNEL_PANIC_READY", "OK"):
                actual = registers(vm)
                if actual["Halted"] and actual["Rax"] == terminal: break
            time.sleep(0.01)
        else: raise TimeoutError("Expected terminal CPU marker missing")
        if case not in ("prepare", "prepare-rollback", "recursive"):
            flush_deadline = time.monotonic() + 3
            while time.monotonic() < flush_deadline:
                log = vm.log()
                if "KERNEL_SMP_PANIC_HALTED" in log.text: break
                if not vm.alive(): raise RuntimeError("VM exited before serial drain")
                time.sleep(0.01)
            else: raise TimeoutError("Completed panic did not reach the serial sink")
        log = vm.log()
        vm.command("stop")
        state_address, record_address = log.number("KERNEL_PANIC_STATE"), log.number("KERNEL_PANIC_RECORD")
        for address, size in ((state_address, 640), (record_address, 512)):
            if address & 63 or not 0xffffffff80000000 <= address <= 0xffffffff80400000 - size:
                raise RuntimeError("Panic state/record outside stable aligned kernel storage")
        state = parent.memory(vm, state_address, 80)
        record = parent.memory(vm, record_address, 64)
        output = parent.memory(vm, log.number("KERNEL_PANIC_OUTPUT"), 1)[0]
        recursion = parent.memory(vm, log.number("KERNEL_PANIC_RECURSION"), 1)[0]
        if state[:3] != [2, (1 << smp) - 1, 1] or any(state[4:7]) or state[8] or any(state[10:73]) or any(state[73:]):
            raise RuntimeError("CPU masks, missing/outcome or unused records disagree with BSP-only profile")
        if record[:3] != [record_address, 1, 0] or any(record[31:]):
            raise RuntimeError("Invalid panic owner/version/reserved fields")
        if case in ("prepare", "prepare-rollback"):
            if case == "prepare-rollback" and log.count("KERNEL_PANIC_PREPARE_ROLLBACK_OK", "OK") != 1:
                raise RuntimeError("Missing explicit rollback/retry evidence")
            if state[3] or state[7] or state[9] or any(record[3:]) or output or recursion:
                raise RuntimeError("Prepared backend already consumed")
        else:
            if state[3] != 1 or state[9] != 2 or record[11] != 8 or record[14] not in (0, 16) or not record[12] & 2:
                raise RuntimeError("First winner or published raw frame invalid")
            if case == "recursive":
                if state[7] != 1 or output or recursion != 1:
                    raise RuntimeError("Recursive panic overwrote/completed the original report")
            elif state[7] != 2 or output != 1 or recursion or "KERNEL_SMP_PANIC_HALTED" not in log.text:
                raise RuntimeError("Panic output did not complete within the bounded writer")
            expected = {"voluntary": 0, "lock-held": 0, "no-simd-call": 0, "opcode": 6, "no-simd": 6,
                        "page": 14, "nmi": 2, "double": 8, "recursive": 6}[case]
            if record[8] != expected or record[9] != 0 or not 0xffffffff80000000 <= record[10] < 0xffffffff80400000:
                raise RuntimeError("Wrong vector/error or instruction location")
            if record[19] != record[13]: raise RuntimeError("Captured GPR RSP differs from normalized frame")
            if case in ("voluntary", "lock-held", "no-simd-call"):
                if record[3] != 1 or record[5] != (1 << 64) - 1 or record[16] != record[5]:
                    raise RuntimeError("Voluntary reason was dereferenced or captured after RCX changed")
            else:
                if record[3] != 2: raise RuntimeError("Origin fault misclassified as remote stop")
                ist = 2 if expected == 2 else 3 if expected == 8 else 1
                base = log.number(f"KERNEL_IST{ist}_TOP") - 16384
                if not base <= record[4] <= base + 16384 - 56:
                    raise RuntimeError("Fault frame outside its private BSP IST")
            if case == "page" and record[6]: raise RuntimeError("Wrong CR2 for null-page injection")
            if case in ("no-simd", "no-simd-call") and not actual["Cr0"] & 8: raise RuntimeError("SIMD guard not enabled")
        arena = parent.ap_monitor.verify(vm, log, parent.memory, smp, True)
        if smp > 1:
            base = log.number("KERNEL_AP_BASE")
            for cpu in range(1, smp):
                address = base + cpu * 0x40000 + 48 * 4096 + 256
                item = parent.memory(vm, address, 64)
                if item[:3] != [address, 1, cpu] or any(item[3:]): raise RuntimeError("Inactive AP record changed")
        return dict(Case=case, ConfiguredVcpus=smp, OsCpusReady=1, Cpu=actual, State=hex(state_address),
                    Record=hex(record_address), Vector=record[8], Phase=state[7], Output=output, Recursion=recursion, ApResources=arena)

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--case", required=True, choices=("prepare","voluntary","opcode","page","nmi","double","no-simd","recursive","lock-held","prepare-rollback","no-simd-call"))
    parser.add_argument("--boots", type=int, default=1, choices=range(1, 11))
    parser.add_argument("--smp", type=int, default=4, choices=(1, 4))
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("panic-firmware-" + uuid.uuid4().hex)
    out.mkdir(); results = []
    try:
        for iteration in range(1, args.boots + 1):
            results.append(boot(args.image, out, iteration, args.case, args.smp))
            print(f"PASS: panic {args.case} {iteration}/{args.boots}; BSP execution, raw record and terminal CPU verified", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "panic-bsp", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "panic-bsp", args.image, results, len(results), args.boots): raise SystemExit(1)

if __name__ == "__main__": main()
