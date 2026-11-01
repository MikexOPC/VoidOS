"""Expected AP arena guard/NX page faults, four configured CPUs, BSP executes the injection."""
import argparse
import importlib.util
import uuid
import time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[7]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--case", choices=("guard", "nx"), required=True)
    parser.add_argument("--boots", type=int, default=1, choices=range(1, 11))
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("ap-resources-fault-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(args.boots):
            with qemu.Vm(out / f"boot-{index + 1}", args.image, "std", smp=4) as vm:
                deadline = time.monotonic() + 120
                while time.monotonic() < deadline:
                    log = vm.log()
                    if log.has("KERNEL_EXCEPTION_HALTED", "FAIL"):
                        break
                    if not vm.alive() or (log.terminal() and not log.has("KERNEL_AP_FAULT_ARMED")):
                        raise RuntimeError("Unexpected termination before AP fault injection")
                    time.sleep(0.01)
                else:
                    raise TimeoutError("Expected AP exception did not complete")
                expected = (0xffff000000000000 | 487 << 39) + 0x200000 + 0x40000
                error = 0
                if args.case == "nx":
                    expected += 4096
                    error = 17
                if not log.has("KERNEL_AP_FAULT_ARMED") or not log.has("KERNEL_AP_RESOURCES_PREPARED", "OK"):
                    raise RuntimeError("Fault before resource verification and injection")
                if log.has("KERNEL_READY") or log.has("AP_EXPECTED_FAULT_MISSING"):
                    raise RuntimeError("Kernel continued after injected fault")
                if log.number("FAULT_VECTOR") != 14 or log.number("FAULT_ERROR") != error or log.number("FAULT_CR2") != expected:
                    raise RuntimeError("Wrong exception/address/error for AP guard or NX")
                if log.has("EXCEPTION_FRAME_INVALID"):
                    raise RuntimeError("Invalid exception frame")
                results.append(dict(Case=args.case, Vector=14, Error=error, Address=hex(expected), OsCpusReady=1))
                print(f"PASS: expected AP {args.case} fault {index + 1}/{args.boots}", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "ap-resources-fault", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-resources-fault", args.image, results, len(results), args.boots):
        raise SystemExit(1)

if __name__ == "__main__":
    main()
