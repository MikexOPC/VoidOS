"""Owned QEMU/OVMF keyboard input test. No host-device access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid
import sys

IDLE_SECONDS = 80  # set by --idle-seconds
sys.dont_write_bytecode = True


# ====== shared monitor protocol ======

import importlib.util
_layout_spec = importlib.util.spec_from_file_location("os_layout", Path(__file__).resolve().parents[7] / "scripts/lib/layout.py")
layout = importlib.util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)
_protocol_spec = importlib.util.spec_from_file_location("ps2_firmware_protocol",Path(__file__).with_name("protocol.py"))
_protocol = importlib.util.module_from_spec(_protocol_spec)
_protocol_spec.loader.exec_module(_protocol)
read_serial, wait_marker, command = _protocol.read_serial, _protocol.wait_marker, _protocol.command
image_hash, number = _protocol.image_hash, _protocol.number
cancel_input, exit_input = _protocol.cancel_input, _protocol.exit_input


# ====== run_boot ======

def run_boot(project, image, qemu, out, index, stress, text_profile, line_stress, shell, console_draw=False,
             console_shell=False, video_absent=False, idle_race=False, idle_rollover=False, mouse=False, pointer=False):
    scenario = out / f"boot-{index}"
    esp = scenario / "esp" / "EFI" / "BOOT"
    esp.mkdir(parents=True)
    loader = project / "arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"
    shutil.copy2(loader, esp / "BOOTX64.EFI")
    shutil.copy2(image, esp / "kernel.elf")
    if image_hash(esp / "kernel.elf") != image_hash(image):
        raise RuntimeError("ESP image mismatch")
    (esp / "boot-check.txt").write_bytes(b"OS boot check\r\n")
    variables = scenario / "vars.fd"
    shutil.copy2(qemu / "share/edk2-i386-vars.fd", variables)
    serial = scenario / "serial.log"
    trace = scenario / "qemu.log"
    args = [str(qemu / "qemu-system-x86_64.exe"), "-machine", "q35", "-cpu", "max",
            "-accel", "tcg", "-m", "128", "-smp", "1", "-net", "none",
            "-display", "none", "-monitor", "stdio", "-no-reboot",
            "-serial", "file:" + serial.as_posix(), "-d", "int,cpu_reset", "-D", trace.as_posix(),
            "-drive", "if=pflash,format=raw,readonly=on,file=" + (qemu / "share/edk2-x86_64-code.fd").as_posix(),
            "-drive", "if=pflash,format=raw,file=" + variables.as_posix(),
            "-drive", "format=raw,file=fat:rw:" + (scenario / "esp").as_posix()]
    if video_absent:
        args += ['-vga', 'none']
    vm = None
    with (scenario / "monitor.log").open("w") as monitor, (scenario / "stderr.log").open("w") as error:
        try:
            vm = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=monitor, stderr=error,
                                  text=True, creationflags=subprocess.CREATE_NO_WINDOW)
            wait_marker(vm, serial, "KERNEL_PS2_TASK_POLL_READY")
            mouse_expected = None
            pointer_frames = []
            if mouse:
                import importlib.util
                spec = importlib.util.spec_from_file_location('mouse_firmware',
                    project / 'kernel/drivers/input/ps2/scripts/mouse/firmware.py')
                mouse_firmware = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mouse_firmware)
                mouse_expected = mouse_firmware.exercise(vm, serial, command, wait_marker, read_serial)
                if pointer and not video_absent:
                    spec = importlib.util.spec_from_file_location('pointer_firmware', project / 'kernel/console/video/scripts/pointer/firmware.py')
                    pointer_firmware = importlib.util.module_from_spec(spec); spec.loader.exec_module(pointer_firmware)
                    pointer_frames = pointer_firmware.exercise(vm,scenario,serial,mouse_expected,read_serial,wait_marker,command,mouse_firmware)
            if idle_rollover:
                idle_deadline = time.monotonic() + IDLE_SECONDS
                while time.monotonic() < idle_deadline:
                    wait_marker(vm, serial, "KERNEL_PS2_TASK_POLL_READY", 1)
                    time.sleep(0.05)
            command(vm, "sendkey a 30")
            wait_marker(vm, serial, "PS2_KEY: 0x000000000000001c")
            time.sleep(0.5)  # Normal empty input must survive multiple HPET acknowledgements.
            for key, code in [("b", 0x32), ("up", 0x175), ("print", 0x17c), ("pause", 0x200)]:
                command(vm, "sendkey " + key + " 30")
                if mouse:
                    mouse_firmware.interleave(vm, serial, command, wait_marker, read_serial, mouse_expected)
                wait_marker(vm, serial, f"PS2_KEY: 0x{code:016x}", 5)
                time.sleep(0.15)
            for key_index in range(stress):
                command(vm, "sendkey a 1")
                wait_marker(vm, serial, "PS2_KEY: ", 5, 6 + key_index)
                time.sleep((0.083, 0.097, 0.101, 0.109, 0.123)[key_index % 5] if idle_race else 0.02)
            extra_keys = 0
            expected_text = [0x61, 0x62] + [0x61] * stress
            if text_profile:
                sequences = [("shift-a", 0x41), ("shift_r-a", 0x41),
                             ("caps_lock", None), ("a", 0x41), ("shift-a", 0x61),
                             ("caps_lock", None), ("bracket_left", 0xe8),
                             ("shift-bracket_left", 0xe9), ("apostrophe", 0xe0),
                             ("alt_r-semicolon", 0x40), ("alt_r-apostrophe", 0x23),
                             ("alt_r-e", 0x20ac), ("alt_r-bracket_left", 0x5b),
                             ("shift-alt_r-bracket_left", 0x7b),
                             ("ctrl-a", None), ("backspace", None), ("tab", None), ("ret", None)]
                for keys, scalar in sequences:
                    extra_keys += len(keys.split("-"))
                    command(vm, "sendkey " + keys + " 20")
                    wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                    if scalar is not None:
                        expected_text.append(scalar)
                    time.sleep(0.12)
                expected_lines = ["".join(chr(value) for value in expected_text[:-1]), "", "ciao"]
                for keys, scalar in [("ret", None), ("c", 0x63), ("i", 0x69), ("a", 0x61),
                                     ("o", 0x6f), ("o", 0x6f), ("backspace", None),
                                     ("bracket_left", 0xe8), ("backspace", None), ("ret", None)]:
                    extra_keys += 1
                    command(vm, "sendkey " + keys + " 20")
                    wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                    if scalar is not None:
                        expected_text.append(scalar)
                    time.sleep(0.12)
                if line_stress:
                    for key_index in range(257):
                        extra_keys += 1
                        key = "a" if key_index < 256 else "b"
                        command(vm, "sendkey " + key + " 1")
                        wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                        expected_text.append(ord(key))
                        time.sleep(0.02)
                    extra_keys += 1
                    command(vm, "sendkey ret 20")
                    wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                    time.sleep(0.12)
                    expected_lines.append("a" * 256)
                    # The two-byte accent straddles the128-byte logging chunk.
                    for key_index in range(255):
                        extra_keys += 1
                        key = "bracket_left" if key_index == 127 else "a"
                        command(vm, "sendkey " + key + " 1")
                        wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                        expected_text.append(0xe8 if key_index == 127 else 0x61)
                        time.sleep(0.02)
                    extra_keys += 1
                    command(vm, "sendkey ret 20")
                    wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                    time.sleep(0.12)
                    expected_lines.append("a" * 127 + "è" + "a" * 127)
                if shell:
                    for line in ["help", "echo ciao è €", "status", "help x", "echo", "echo exit", "exit"]:
                        expected_lines.append(line)
                        for character in line:
                            key = {" ": "spc", "è": "bracket_left", "€": "alt_r-e"}.get(character, character)
                            extra_keys += len(key.split("-"))
                            command(vm, "sendkey " + key + " 20")
                            wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                            expected_text.append(ord(character))
                            time.sleep(0.08)
                        extra_keys += 1
                        command(vm, "sendkey ret 20")
                        wait_marker(vm, serial, "PS2_KEY: ", 5, 5 + stress + extra_keys)
                        time.sleep(0.12)
                        if pointer and not video_absent and line in ('status','echo exit'):
                            pointer_frames.append(pointer_firmware.verify(vm,scenario,serial,mouse_expected,read_serial,wait_marker,command,line.replace(' ','-')))
            time.sleep(0.05)
            if not shell:
                exit_input(vm,serial)
                extra_keys += 5
                if text_profile:
                    expected_text.extend(map(ord,"exit"))
                    expected_lines.append("exit")
            log = wait_marker(vm, serial, "KERNEL_READY", 10)
            for marker in ["KERNEL_PS2_INPUT_READY", "KERNEL_INPUT_TASKS_COLLECTED",
                           "KERNEL_PS2_INPUT_CLEANUP_OK", "KERNEL_SCHEDULER_CLEANUP_OK",
                           "SHELL_TASK2_READY", "SHELL_TASK2_CLOSED_EMPTY"]:
                if marker not in log:
                    raise RuntimeError("Missing " + marker)
            counters = {name: number(log, name) for name in
                        ["PS2_BYTES", "PS2_KEYS", "PS2_RELEASES", "PS2_TASK_IRQS", "PS2_IDLE", "SCHEDULER_DISPATCHES", "PS2_RESYNCS", "PS2_DEVICE_ERRORS", "PS2_DISCARDED"]}
            if idle_rollover:
                fired = number(log, "KERNEL_WATCHDOG_FIRED")
                ticks = number(log, "KERNEL_WATCHDOG_TICKS")
                if not 0 < ticks < 0x80000000 or fired * ticks <= (1 << 32):
                    raise RuntimeError("No proven full 32-bit counter traversal")
                counters["WatchdogDeadlines"] = fired
                counters["MinimumCounterAdvance"] = fired * ticks
            # QEMU's i8042 never produces parity/timeout/burst faults, so the recoverable-fault paths
            # (B2) must stay idle in a healthy session.
            if counters["PS2_RESYNCS"] or counters["PS2_DEVICE_ERRORS"] or counters["PS2_DISCARDED"]:
                raise RuntimeError("Unexpected keyboard faults: " + str(counters))
            collected = log.index("KERNEL_INPUT_TASKS_COLLECTED")
            session_log = log[:collected].rsplit("# ============ scheduler cooperativo BSP", 1)[-1]
            counters["BudgetBefore"] = number(session_log, "SCHEDULER_BUDGET_BEFORE")
            counters["BudgetAfter"] = number(session_log, "SCHEDULER_BUDGET_AFTER")
            if counters["BudgetBefore"] != counters["BudgetAfter"]:
                raise RuntimeError("Input session physical budget mismatch")
            expected_keys = 5 + int(not shell) + stress + extra_keys
            # Pause has no break; stopping may disable scanning before the
            # final Enter break. All other injected releases are required.
            if counters["PS2_KEYS"] != expected_keys or counters["PS2_RELEASES"] < expected_keys - 2 or (not video_absent and not counters["PS2_TASK_IRQS"]) or counters["PS2_IDLE"] < 2:
                raise RuntimeError("Input/IRQ/idle evidence insufficient: " + str(counters))
            if text_profile:
                actual = [int(value, 16) for value in re.findall(r"PS2_UNICODE: 0x([0-9a-fA-F]{16})", log)]
                utf8 = re.findall(r"^PS2_TEXT: (.*)$", log, re.MULTILINE)
                if actual != expected_text or utf8 != [chr(value) for value in expected_text]:
                    raise RuntimeError("Italian Unicode/UTF-8 sequence mismatch: " + str(actual))
                counters["TextCount"] = number(log, "PS2_TEXT_COUNT")
                if counters["TextCount"] != len(expected_text):
                    raise RuntimeError("Text accounting mismatch")
                for edit in ["BACKSPACE", "TAB", "ENTER"]:
                    if "PS2_EDIT_" + edit not in log:
                        raise RuntimeError("Missing typed editing action: " + edit)
                lines = re.findall(r"^PS2_LINE_READY: (.*)$", log, re.MULTILINE)
                lengths = [int(value, 16) for value in re.findall(r"PS2_LINE_LENGTH: 0x([0-9a-fA-F]{16})", log)]
                if lines != expected_lines or lengths != [len(value.encode("utf-8")) for value in expected_lines]:
                    raise RuntimeError("Edited line or UTF-8 byte length mismatch: " + str(lines))
                counters["Lines"] = number(log, "PS2_LINE_COUNT")
                counters["LineFull"] = number(log, "PS2_LINE_FULL_COUNT")
                if counters["Lines"] != len(expected_lines) or counters["LineFull"] != int(line_stress):
                    raise RuntimeError("Line delivery/full accounting mismatch")
                counters["ShellCommands"] = number(log, "SHELL_COMMAND_COUNT")
                if counters["ShellCommands"] != len(expected_lines):
                    raise RuntimeError("Shell queue delivery mismatch")
                if shell:
                    for marker in ["SHELL_HELP:", "SHELL_STATUS:", "SHELL_BAD_ARGUMENTS",
                                   "SHELL_UNKNOWN_COMMAND", "SHELL_EXIT_REQUEST"]:
                        if marker not in log:
                            raise RuntimeError("Missing shell response: " + marker)
                    if re.findall(r"^SHELL_ECHO: (.*)$",log,re.MULTILINE) != ["ciao è €", "", "exit"]:
                        raise RuntimeError("Literal UTF-8 echo mismatch")
                if line_stress and log.count("SHELL_LINE_REJECTED_TRUNCATED") != 1:
                    raise RuntimeError("Truncated command was not rejected")
            command(vm, "stop")
            command(vm, "info registers")
            if pointer:
                if video_absent:
                    if 'KERNEL_POINTER_OPEN' in log: raise RuntimeError('Pointer initialized without video')
                    counters['Pointer'] = {'Absent': True}
                else:
                    counters['Pointer'] = {**pointer_firmware.finish(log,mouse_expected), 'Frames': pointer_frames}
            if console_shell:
                if video_absent:
                    if 'KERNEL_CONSOLE_SHELL_SERIAL_CLEAN' not in log or 'KERNEL_CONSOLE_SHELL_OPEN' in log:
                        raise RuntimeError('Invalid serial fallback lifetime')
                    counters['ConsoleShell'] = {'Absent': True}
                else:
                    import importlib.util
                    spec = importlib.util.spec_from_file_location('shell_pixels',
                        project / 'kernel/console/video/scripts/shell/pixels.py')
                    pixels = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(pixels)
                    counters['ConsoleShell'] = pixels.verify(vm, scenario, log)
            if console_draw:
                import importlib.util
                spec = importlib.util.spec_from_file_location('console_cell',
                    project / 'kernel/console/video/scripts/cell.py')
                cell = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(cell)
                counters['ConsoleDraw'] = cell.verify(vm, scenario, log)
            command(vm, "quit")
            vm.wait(timeout=5)
            cpu = trace.read_text(errors="replace")
            if "Triple fault" in cpu:
                raise RuntimeError("Unexpected triple fault")
            if len(re.findall(r"Servicing hardware INT=0x41", cpu)) < 4:
                raise RuntimeError("Hardware input IRQ trace missing")
            if mouse:
                counters['Mouse'] = mouse_firmware.verify(log, cpu, mouse_expected, number, task_irq_required=not video_absent)
            (scenario / "results.json").write_text(json.dumps(counters, indent=2))
            return counters
        except Exception:
            if vm is not None and vm.poll() is None:
                try:
                    command(vm, "stop")
                    command(vm, "info registers")
                    command(vm, "info lapic")
                    command(vm, "info irq")
                    owner = re.search(r'KERNEL_WATCHDOG_OWNER: 0x([0-9a-fA-F]{16})', read_serial(serial))
                    if owner and layout.KERNEL_IMAGE_BASE <= int(owner[1], 16) < layout.KERNEL_IMAGE_LIMIT:
                        command(vm, "x /28gx 0x" + owner[1])
                    time.sleep(0.2)
                except (OSError, ValueError):
                    pass
            raise
        finally:
            if vm is not None:
                if vm.poll() is None:
                    vm.kill()
                vm.wait(timeout=5)
                vm.stdin.close()


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--qemu", type=Path, default=Path("C:/Program Files/qemu"))
    parser.add_argument("--mouse", action="store_true")
    parser.add_argument("--pointer", action="store_true")
    parser.add_argument("--stress", type=int, default=0, choices=range(201))
    parser.add_argument("--idle-race", action="store_true")
    parser.add_argument("--idle-rollover", action="store_true")
    parser.add_argument("--idle-seconds", type=int, default=80, help="idle time of --idle-rollover; 240 passes the old 164 s session cap")
    parser.add_argument("--text", action="store_true")
    parser.add_argument("--line-stress", action="store_true")
    parser.add_argument("--shell", action="store_true")
    parser.add_argument("--console-draw", action="store_true")
    parser.add_argument("--console-shell", action="store_true")
    parser.add_argument("--video-absent", action="store_true")
    args = parser.parse_args()
    global IDLE_SECONDS
    IDLE_SECONDS = args.idle_seconds
    if args.pointer and not (args.mouse and args.console_shell): parser.error('--pointer requires --mouse --console-shell')
    if args.mouse and args.console_draw:
        parser.error("Mouse runtime excludes --console-draw diagnostic")
    if args.idle_race and not args.stress:
        parser.error('--idle-race requires --stress')
    if (args.line_stress or args.shell) and not args.text:
        parser.error("--line-stress/--shell require --text")
    if args.console_draw and not (args.text and args.shell):
        parser.error("--console-draw requires --text and --shell")
    if args.console_shell and (not (args.text and args.shell) or args.console_draw):
        parser.error('--console-shell requires --text --shell, excludes --console-draw')
    if args.video_absent and not args.console_shell:
        parser.error('--video-absent requires --console-shell')
    project = Path(__file__).resolve().parents[7]
    out = project / "kernel/build" / ("ps2-input-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    manifest = {"Image": image_hash(args.image), "Boots": args.boots, "StressKeys": args.stress,
                "Mouse": args.mouse, "Pointer": args.pointer, "IdleRace": args.idle_race,
                "IdleRollover": args.idle_rollover,
                "ItalianText": args.text,
                "LineStress": args.line_stress,
                "Shell": args.shell,
                "ConsoleDraw": args.console_draw,
                "ConsoleShell": args.console_shell, "VideoAbsent": args.video_absent,
                "Loader": image_hash(project / "arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"),
                "Qemu": image_hash(args.qemu / "qemu-system-x86_64.exe"),
                "OVMF": image_hash(args.qemu / "share/edk2-x86_64-code.fd")}
    (out / "images.json").write_text(json.dumps(manifest, indent=2))
    results = []
    for index in range(1, args.boots + 1):
        results.append(run_boot(project, args.image, args.qemu, out, index, args.stress, args.text,
            args.line_stress, args.shell, args.console_draw, args.console_shell, args.video_absent,
            args.idle_race, args.idle_rollover, args.mouse, args.pointer))
        print(f"PASS: PS/2 QEMU {index}/{args.boots}, keys/IRQ/idle/cleanup", flush=True)
    (out / "results.json").write_text(json.dumps({**manifest, "Results": results}, indent=2))
    print("PASS: PS/2 input firmware evidence " + str(out), flush=True)


if __name__ == "__main__":
    main()
