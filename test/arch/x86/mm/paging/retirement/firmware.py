"""K1 stage 5b: full lower-half removal and a warmed identity-page fault."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[6]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


# ====== registers ======

def registers(vm):
    offset = vm.monitor.stat().st_size
    vm.command("stop")
    vm.command("info registers")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        text = vm.monitor.read_bytes()[offset:].decode(errors="replace")
        if "EFER=" in text:
            return text
        time.sleep(0.005)
    raise TimeoutError("Missing CPU registers")


# ====== boot ======

def boot(image, out, index, negative):
    with qemu.Vm(out / f"boot-{index}", image, "std", memory=128 if index % 2 else 256) as vm:
        if negative:
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                log = vm.log()
                if log.has("KERNEL_EXCEPTION_HALTED"): break
                if log.has("KERNEL_PANIC") or not vm.alive():
                    raise RuntimeError("Unexpected panic or VM exit")
                time.sleep(0.01)
            else: raise TimeoutError("Missing retired capsule fault")
            if log.number("FAULT_VECTOR") != 14 or log.number("FAULT_ERROR") != 0:
                raise RuntimeError("Expected supervisor read of a nonpresent page")
            if log.number("FAULT_CR2") != log.number("KERNEL_RETIRED_CAPSULE"):
                raise RuntimeError("Fault address is not the retired capsule")
            for marker in ("KERNEL_RETIRED_CAPSULE_WARMED", "KERNEL_RETIRED_CAPSULE_READ_ARMED"):
                if not log.has(marker): raise RuntimeError("Missing " + marker)
            if "EXPECTED_LOADER_FAULT_MISSING" in log.text: raise RuntimeError("Stale TLB translation")
        else:
            log = vm.wait("KERNEL_READY", 120)
            if log.failures(): raise RuntimeError("Unexpected failures: " + str(log.failures()))
        for marker in ("KERNEL_LOWER_HALF_EMPTY", "KERNEL_LOADER_IDENTITY_RETIRED"):
            if not log.has(marker, "OK"): raise RuntimeError("Missing " + marker)
        text = registers(vm)
        root = int(re.search(r"CR3=([0-9a-fA-F]+)", text)[1], 16)
        cr4 = int(re.search(r"CR4=([0-9a-fA-F]+)", text)[1], 16)
        if cr4 & ((1 << 7) | (1 << 12) | (1 << 17)): raise RuntimeError("Unexpected CR4 profile")
        entries = vm.physical(root, 512)
        if any(entries[:256]): raise RuntimeError("Lower PML4 not empty")
        if not entries[510] & 1 or not entries[511] & 1: raise RuntimeError("Upper mappings missing")
        for name in ("GDT", "IDT"):
            match = re.search(name + r"=\s*([0-9a-fA-F]+)", text)
            if not match or int(match[1], 16) < 0xffff800000000000:
                raise RuntimeError(name + " still low")
        tr = re.search(r"TR\s*=\s*0018\s+([0-9a-fA-F]+)", text)
        if not tr or int(tr[1], 16) < 0xffff800000000000: raise RuntimeError("TSS still low")
        return dict(Root=hex(root), LowerSlots=0, Negative=negative, Cr4=hex(cr4))


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--negative", action="store_true")
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("paging-retirement-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(1, args.boots + 1):
            results.append(boot(args.image, out, index, args.negative))
            print(f"PASS: loader retirement {index}/{args.boots}", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "paging-retirement", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "paging-retirement", args.image, results, len(results), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
