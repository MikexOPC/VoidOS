"""G5: a heap PTE changed outside the APIs is caught by the idle paging audit within its period.

Image: -Ps2Input -Ps2Mouse -ConsoleShell -InteractiveInput -TestPagingTamper. Mouse moves keep the
console flushing; after 200 flushes the image tampers, the next audit (at most 1 s later) must
report KERNEL_PAGING_AUDIT_FAILED and the input worker must stop with stage 6.
"""
import argparse
import importlib.util
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


# ====== boot ======

def boot(image, out, index):
    with qemu.Vm(out / f"boot-{index}", image, "std") as vm:
        # The expected stop is a terminal state for Vm.wait: poll the log directly.
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            log = vm.log()
            if log.has("KERNEL_PAGING_AUDIT_FAILED", "FAIL"): break
            if log.has("KERNEL_PAGING_TAMPER_FAILED") or not vm.alive(): raise RuntimeError("Tamper hook failed or VM exited")
            if log.has("KERNEL_POINTER_READY"): vm.command("mouse_move 1 0" if int(time.monotonic() * 10) % 2 else "mouse_move -1 0")
            time.sleep(0.01)
        else:
            raise RuntimeError("Tampered PTE not caught by the audit")
        time.sleep(0.5)
        log = vm.log()
        if not log.before("KERNEL_PAGING_TAMPERED", "KERNEL_PAGING_AUDIT_FAILED"): raise RuntimeError("Audit failed before the tamper")
        if log.number("PS2_WORKER_FAILURE_STAGE") != 6 or not log.has("PS2_INPUT_FAILED_WORKER", "FAIL"):
            raise RuntimeError("The session did not stop on the audit")
        tampered, caught = log.line("KERNEL_PAGING_TAMPERED"), log.line("KERNEL_PAGING_AUDIT_FAILED")
        return dict(TamperLine=tampered, AuditLine=caught)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=3, choices=range(1, 21))
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("paging-tamper-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(1, args.boots + 1):
            results.append(boot(args.image, out, index))
            print(f"PASS: paging tamper caught {index}/{args.boots}", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "paging-tamper", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "paging-tamper", args.image, results, len(results), args.boots): raise SystemExit(1)


if __name__ == "__main__":
    main()
