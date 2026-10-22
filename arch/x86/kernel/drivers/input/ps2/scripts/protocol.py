"""Owned QEMU monitor/serial protocol and input-session lifecycle."""
import hashlib
import re
import time

# ====== read_serial ======

def read_serial(path):
    raw = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    return re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", raw)


# ====== wait_marker ======

def wait_marker(vm, serial, marker, timeout=40, min_keys=0, occurrences=1):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        log = read_serial(serial)
        if re.search(r"KERNEL_EXCEPTION_HALTED|\bPANIC\b|DISPATCH_FAILED|PS2_INPUT_FAILED|KERNEL_CONSOLE_DRAW_REJECTED_PM", log):
            raise RuntimeError("Kernel failure: " + str(serial))
        if log.count(marker) >= occurrences and log.count("PS2_KEY: ") >= min_keys:
            return log
        if vm.poll() is not None:
            raise RuntimeError("Owned VM exited before marker")
        time.sleep(0.001)
    raise TimeoutError("Missing " + marker + ": " + str(serial))


# ====== command ======

def command(vm, text):
    vm.stdin.write(text + "\n")
    vm.stdin.flush()


# ====== image_hash ======

def image_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


# ====== number ======

def number(log, field):
    match = re.search(re.escape(field) + r": 0x([0-9a-fA-F]{16})", log)
    if not match:
        raise RuntimeError("Missing numeric evidence: " + field)
    return int(match[1], 16)


# ====== cancel_input ======

def cancel_input(vm, serial):
    count = read_serial(serial).count("PS2_EDIT_CANCEL")
    command(vm,"sendkey esc 20")
    return wait_marker(vm,serial,"PS2_EDIT_CANCEL",5,occurrences=count+1)


# ====== exit_input ======

def exit_input(vm, serial):
    cancel_input(vm,serial)
    for key in ("e","x","i","t","ret"):
        count = read_serial(serial).count("PS2_KEY: ")
        command(vm,"sendkey "+key+" 20")
        wait_marker(vm,serial,"PS2_KEY: ",5,min_keys=count+1)
        time.sleep(0.04)
    return wait_marker(vm,serial,"KERNEL_READY",30)
