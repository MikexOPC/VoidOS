import re
import time


# ====== packets ======

def packets(log):
    return [tuple(int(value, 16) for value in match) for match in re.findall(
        r"^MOUSE_EVENT: (-?0x[0-9a-fA-F]{16}) (-?0x[0-9a-fA-F]{16}) (0x[0-9a-fA-F]{16})$",
        log, re.MULTILINE)]


# ====== exercise ======

def exercise(vm, serial, command, wait_marker, read_serial):
    expected = []
    buttons = 0
    operations = [("mouse_move 17 9", (17, -9, 0)),
                  ("mouse_button 1", (0, 0, 1)),
                  ("mouse_move -23 -11", (-23, 11, 1)),
                  ("mouse_button 3", (0, 0, 3)),
                  ("mouse_button 7", (0, 0, 7)),
                  ("mouse_move 127 -127", (127, 127, 7)),
                  ("mouse_button 0", (0, 0, 0))]
    for index in range(24):
        dx, dy = (index % 17) - 8, (index % 13) - 6
        operations.append((f"mouse_move {dx} {dy}", (dx, -dy, buttons)))
    for operation, packet in operations:
        command(vm, operation)
        expected.append(packet)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            wait_marker(vm, serial, "KERNEL_PS2_TASK_POLL_READY", 1)
            actual = packets(read_serial(serial))
            if len(actual) >= len(expected):
                if actual != expected:
                    raise RuntimeError(f"Mouse packet mismatch: {actual} != {expected}")
                break
            time.sleep(0.001)
        else:
            raise TimeoutError("Mouse packet deadline")
        time.sleep(0.025)
    return expected


# ====== interleave ======

def interleave(vm, serial, command, wait_marker, read_serial, expected):
    command(vm, "mouse_move -5 4")
    expected.append((-5, -4, 0))
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        wait_marker(vm, serial, "KERNEL_PS2_TASK_POLL_READY", 1)
        actual = packets(read_serial(serial))
        if len(actual) >= len(expected):
            if actual != expected:
                raise RuntimeError("Mouse/keyboard interleaving mismatch")
            return
        time.sleep(0.001)
    raise TimeoutError("Interleaved mouse deadline")


# ====== verify ======

def verify(log, trace, expected, number, task_irq_required=True):
    if packets(log) != expected:
        raise RuntimeError("Mouse final sequence mismatch")
    counters = {name: number(log, name) for name in
                ["MOUSE_BYTES", "MOUSE_EVENTS", "MOUSE_IRQS", "MOUSE_LOSSES", "MOUSE_TASK_IRQS"]}
    if (counters["MOUSE_BYTES"] != len(expected) * 3 or counters["MOUSE_EVENTS"] != len(expected)
            or not counters["MOUSE_IRQS"] or (task_irq_required and not counters["MOUSE_TASK_IRQS"]) or counters["MOUSE_LOSSES"]):
        raise RuntimeError("Mouse accounting mismatch: " + str(counters))
    if "KERNEL_PS2_MOUSE_CLEANUP_OK" not in log or not re.search("Servicing hardware INT=0x42", trace):
        raise RuntimeError("Mouse IRQ12/cleanup evidence missing")
    return counters
