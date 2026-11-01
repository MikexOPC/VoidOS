"""K2-A: IF real on the BSP; host threads, not guest APs, exercise contention."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("os_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


# ====== boot ======

def boot(image, out, index, mode):
    with qemu.Vm(out / f"boot-{index}", image, "std", memory=128 if index % 2 else 256) as vm:
        reason = {"wait": "LOCK_WAIT_FAILED", "irq-enabled": "LOCK_RELEASE_IRQ_ENABLED"}.get(mode)
        timings = []
        if reason:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                log = vm.log()
                if log.has(reason, "FAIL"): break
                if not vm.alive() or log.has("KERNEL_EXCEPTION_HALTED"):
                    raise RuntimeError("Unexpected exception or VM exit")
                time.sleep(0.01)
            else: raise TimeoutError("Missing expected lock fail-stop")
            if mode == "wait" and (not log.has("KERNEL_PERCPU_WAIT_READY", "OK") or
                                   not log.has("KERNEL_PERCPU_WAIT_TIMEOUT_ARMED")):
                raise RuntimeError("Failure before native per-CPU PM timeout injection")
            if not log.has("KERNEL_SYNC_EXPECTED_FAILURE_ARMED"):
                raise RuntimeError("Failure before injection")
            if "KERNEL_SYNC_EXPECTED_FAILURE_MISSING" in log.text or log.has("KERNEL_READY"):
                raise RuntimeError("Kernel continued after lock failure")
        else:
            log = vm.wait("KERNEL_READY", 90)
            if log.failures(): raise RuntimeError("Unexpected failure: " + str(log.failures()))
        if mode != "ordinary":
            for marker in ("KERNEL_SYNC_BSP_CONTEXT_OK", "KERNEL_SYNC_IF0_RESTORED", "KERNEL_SYNC_IF1_RESTORED",
                           "KERNEL_SYNC_GUARDS_REJECTED", "KERNEL_SYNC_HANDOFF_SIMULATED"):
                if not log.has(marker, "OK"): raise RuntimeError("Missing " + marker)
            if not reason and not log.has("KERNEL_SYNC_PROBE_OK", "OK"):
                raise RuntimeError("Probe did not complete")
        elif "KERNEL_SYNC_" in log.text:
            raise RuntimeError("Diagnostic code leaked into the ordinary profile")
        if not log.has("KERNEL_LOWER_HALF_EMPTY", "OK"):
            raise RuntimeError("K1 retirement regression")
        if log.count("KERNEL_LOCK_CONTEXT_READY", "OK") != 1:
            raise RuntimeError("Missing or repeated bootstrap context initialization")
        if not reason:
            pattern = r"KERNEL_TIMER_TIMING_GOAL: (0x[0-9a-f]+)\s+KERNEL_TIMER_IRQ_PM: (0x[0-9a-f]+)\s+KERNEL_TIMER_WAIT_PM: (0x[0-9a-f]+)"
            timings = [dict(Goal=int(g, 16), IrqPm=int(i, 16), WaitPm=int(w, 16))
                       for g, i, w in re.findall(pattern, log.text)]
            if [t["Goal"] for t in timings] != [1, 2, 3] or any(
                not 35795 // 2 <= t["IrqPm"] <= 35795 * 20 or not t["WaitPm"] for t in timings
            ): raise RuntimeError("Missing or invalid separate IRQ/wait timing evidence")
        return dict(Mode=mode, Memory=128 if index % 2 else 256, ExpectedFailure=reason, Timings=timings)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--case", choices=("positive", "wait", "irq-enabled", "ordinary"), default="positive")
    args = parser.parse_args()
    out = ROOT / "kernel/build" / ("sync-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    try:
        for index in range(1, args.boots + 1):
            results.append(boot(args.image, out, index, args.case))
            print(f"PASS: synchronization {args.case} {index}/{args.boots}", flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out, "kernel-sync", args.image, results, len(results) - 1, args.boots)
        raise
    if not qemu.finish(out, "kernel-sync", args.image, results, len(results), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
