import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[5]

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--expect-reject", action="store_true")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("ps2_firmware",
        ROOT / "arch/x86/kernel/drivers/input/ps2/scripts/firmware.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = module.wait_marker
    state = {"Enabled": False, "Restored": False, "Evidence": None}

    # ====== wait ======

    def wait(vm, serial, marker, timeout=40, min_keys=0):
        log = original(vm, serial, marker, timeout, min_keys)
        if marker == "KERNEL_PS2_TASK_POLL_READY" and not state["Enabled"]:
            module.command(vm, "one-insn-per-tb on")
            state["Enabled"] = True
            state["Evidence"] = serial.parent.parent
            (serial.parent / "slow-tcg.json").write_text(json.dumps({
                "Command": "one-insn-per-tb on", "After": marker,
                "Source": "https://www.qemu.org/docs/master/system/monitor.html",
                "ClockChanged": False}, indent=2), encoding="utf-8")
        elif state["Enabled"] and not state["Restored"]:
            if "KERNEL_CONSOLE_SLOW_GROUP" not in log:
                raise RuntimeError("No verified slow group before input")
            module.command(vm, "one-insn-per-tb off")
            state["Restored"] = True
        return log

    module.wait_marker = wait
    sys.argv = [str(spec.origin), "--image", str(args.image.resolve(strict=True)),
        "--boots", "1", "--text", "--shell", "--console-shell"]
    try:
        module.main()
    except RuntimeError:
        if not args.expect_reject or not state["Evidence"]:
            raise
        log = module.read_serial(state["Evidence"] / "boot-1/serial.log")
        if ("PS2_INPUT_FAILED_CONSOLE_FLUSH" not in log or
            module.number(log, "CONSOLE_FAILURE_PM") <= 357955 or
            module.number(log, "CONSOLE_FAILURE_FLAGS") != 0x1000001 or
            module.number(log, "CONSOLE_FAILURE_RENDERED") <=
                module.number(log, "CONSOLE_FAILURE_CELLS") or
            module.number(log, "PS2_WORKER_FAILURE_ERROR") or
            module.number(log, "PS2_WORKER_FAILURE_LOSSES")):
            raise RuntimeError("Failure is not the expected measured overrun")
        print("PASS expected rejection: verified draw, measured overrun, fail-stop", flush=True)
    else:
        if args.expect_reject:
            raise RuntimeError("Missing expected rejection")
        log = module.read_serial(state["Evidence"] / "boot-1/serial.log")
        if not state["Restored"] or module.number(log, "KERNEL_CONSOLE_SHELL_OVERRUNS") < 1:
            raise RuntimeError("Missing restored execution or overrun accounting")
        print("PASS: slow group WARN, input progress, cleanup and accounting", flush=True)
    print("Evidence: " + str(state["Evidence"]), flush=True)


if __name__ == "__main__":
    main()
