"""G3: kernel heap boot test (-TestBootstrapMemory -TestKernelHeap), on the G2 libraries."""
import argparse
import importlib.util
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


# ====== boot ======

def boot(image, out, index):
    with qemu.Vm(out / f"boot-{index}", image, "std") as vm:
        log = vm.wait("KERNEL_READY", 120)
        for marker in ("KERNEL_HEAP_SELFTEST_OK", "KERNEL_HEAP_READY"):
            if not log.has(marker, "OK"): raise RuntimeError("Missing " + marker)
        if not log.before("KERNEL_HEAP_READY", "KERNEL_READY"): raise RuntimeError("Heap not bound during boot")
        if log.failures(): raise RuntimeError("Failure markers: " + ", ".join(log.failures()))
        values = {name: log.number("KERNEL_HEAP_" + name) for name in ("VIRTUAL", "PAGES", "TABLES", "DATA", "INVENTORY")}
        if values["VIRTUAL"] != (230 << 39) + 0x200000 or values["PAGES"] != 4096 or values["TABLES"] != 10:
            raise RuntimeError("Heap layout")
        if values["DATA"] & 4095 or log.number("KERNEL_HEAP_SELFTEST_REJECTED") != 65:  # 64 double frees + 1 canary
            raise RuntimeError("Heap data or rejected count")
        return {name: hex(value) for name, value in values.items()}


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("kernel-heap-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(1, args.boots + 1):
            results.append(boot(args.image, out, index))
            print(f"PASS: kernel heap {index}/{args.boots}", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "kernel-heap", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "kernel-heap", args.image, results, len(results), args.boots): raise SystemExit(1)


if __name__ == "__main__":
    main()
