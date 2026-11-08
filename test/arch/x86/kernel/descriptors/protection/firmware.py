"""Verify user segments and SMEP/SMAP policy on every CPU of an ordinary boot."""
import argparse
import importlib.util
import re
import time
import uuid
from pathlib import Path

root = Path(__file__).resolve().parents[6]
spec = importlib.util.spec_from_file_location("k6_qemu", root / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)

CR4 = re.compile(rb"CR4=([0-9a-fA-F]+)")
SMEP = 1 << 20
SMAP = 1 << 21
EXPECTED = {"max": SMEP | SMAP, "max,-smap": SMEP, "max,-smep": SMAP, "max,-smep,-smap": 0}


# ====== read_cr4 ======

def read_cr4(vm, cpu):
    vm.command(f"cpu {cpu}")
    offset = vm.monitor.stat().st_size
    vm.command("info registers")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        with vm.monitor.open("rb") as monitor:
            monitor.seek(offset)
            match = CR4.search(monitor.read())
        if match:
            return int(match[1], 16)
        time.sleep(0.01)
    raise RuntimeError(f"Missing CR4 of CPU {cpu}")


# ====== boot ======

def boot(directory, image, model, cpus):
    with qemu.Vm(directory, image, smp=cpus, extra=("-cpu", model)) as vm:
        log = vm.wait("KERNEL_READY", level="OK", timeout=90)
        if log.failures() or log.terminal():
            raise RuntimeError("Unexpected kernel failure")
        expected = EXPECTED[model]
        if log.number("KERNEL_USER_PROTECTION") != expected:
            raise RuntimeError("Unexpected reported protection")
        full = expected == SMEP | SMAP
        if log.has("KERNEL_USER_MODE_ALLOWED") != full or log.has("USER_MODE_DISABLED", "WARN") == full:
            raise RuntimeError("Unexpected user mode decision")
        actual = [read_cr4(vm, cpu) & (SMEP | SMAP) for cpu in range(cpus)]
        if actual != [expected] * cpus:
            raise RuntimeError(f"Unexpected CR4 per CPU: {actual}")
        return dict(Cpu=model, Protection=hex(expected), Cpus=cpus, Allowed=full)


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--boots", default=10, type=int, choices=range(1, 11))
    parser.add_argument("--cpu", default="max", choices=sorted(EXPECTED))
    parser.add_argument("--smp", default=4, type=int)
    args = parser.parse_args()
    out = root / "kernel/build" / ("k6-protection-" + uuid.uuid4().hex)
    out.mkdir()
    results = []
    for index in range(args.boots):
        try:
            results.append(boot(out / f"boot-{index + 1}", args.image.resolve(), args.cpu, args.smp))
        except (OSError, RuntimeError) as error:
            results.append(dict(Failure=str(error)))
            break
    passed = sum("Failure" not in result for result in results)
    return 0 if qemu.finish(out, "k6-protection", args.image, results, passed, args.boots) else 1


if __name__ == "__main__":
    raise SystemExit(main())
