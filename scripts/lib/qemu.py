"""G2 Q2: one owned QEMU/OVMF virtual machine per boot.

Vm prepares an isolated ESP (hash-checked kernel, current loader, fresh OVMF variables), builds
the arguments of a video profile, and kills only the process it started. Serial evidence is
read through scripts/lib/serial.py.
"""
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QEMU = Path("C:/Program Files/qemu")
LOADER = ROOT / "arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"

_spec = importlib.util.spec_from_file_location("os_serial_log", Path(__file__).with_name("serial.py"))
serial = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(serial)

# QEMU v11.1.0 trace events of the VT-d unit (V2b).
VTD_EVENTS = ("vtd_dmar_enable", "vtd_switch_address_space", "vtd_frr_new", "vtd_reg_dmar_root")

# Video profiles: std (GOP only), virtio (VirtIO GPU without IOMMU), iommu (VirtIO GPU behind
# intel-iommu, which must come first on the command line).
VIDEO = {
    "std": ["-vga", "std"],
    "virtio": ["-vga", "none", "-device", "virtio-vga"],
    "iommu": ["-vga", "none", "-device", "intel-iommu,intremap=off,caching-mode=on,device-iotlb=off",
        "-device", "virtio-vga,iommu_platform=on"],
}


# ====== image_hash ======

def image_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


# ====== Vm ======

class Vm:
    """Context manager: with Vm(directory, image, "iommu") as vm: vm.wait("KERNEL_READY")."""

    def __init__(self, directory, image, video="std", memory=128, smp=1, traces=(), extra=()):
        if video not in VIDEO: raise ValueError("Unknown video profile: " + video)
        self.directory = Path(directory)
        self.image = Path(image)
        self.serial = self.directory / "serial.log"
        self.monitor = self.directory / "monitor.log"
        self.trace = self.directory / "qemu.log"
        self.process = None
        esp = self.directory / "esp/EFI/BOOT"
        esp.mkdir(parents=True)
        shutil.copy2(self.image, esp / "kernel.elf")
        shutil.copy2(LOADER, esp / "BOOTX64.EFI")
        if image_hash(self.image) != image_hash(esp / "kernel.elf"): raise RuntimeError("ESP image mismatch")
        (esp / "boot-check.txt").write_bytes(b"OS boot check\r\n")
        variables = self.directory / "vars.fd"
        shutil.copy2(QEMU / "share/edk2-i386-vars.fd", variables)
        events = list(VTD_EVENTS) if video == "iommu" else []
        self.arguments = [str(QEMU / "qemu-system-x86_64.exe"), "-machine", "q35", "-cpu", "max",
            "-accel", "tcg", "-m", str(memory), "-smp", str(smp), "-net", "none"] + VIDEO[video] + \
            [item for event in events for item in ("-trace", "enable=" + event)] + [
            "-display", "none", "-monitor", "stdio", "-no-reboot", "-serial", "file:" + self.serial.as_posix(),
            "-d", "int,cpu_reset", "-D", self.trace.as_posix()] + \
            [item for event, path in traces for item in ("-trace", "enable=" + event + ",file=" + Path(path).as_posix())] + [
            "-drive", "if=pflash,format=raw,readonly=on,file=" + (QEMU / "share/edk2-x86_64-code.fd").as_posix(),
            "-drive", "if=pflash,format=raw,file=" + variables.as_posix(),
            "-drive", "format=raw,file=fat:rw:" + (self.directory / "esp").as_posix()] + list(extra)

    # ====== lifecycle ======

    def __enter__(self):
        self._monitor = self.monitor.open("w")
        self._error = (self.directory / "stderr.log").open("w")
        self.process = subprocess.Popen(self.arguments, stdin=subprocess.PIPE, stdout=self._monitor, stderr=self._error,
            text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return self

    def __exit__(self, *exception):
        self.close()
        return False

    def close(self):
        if self.process is not None:
            if self.process.poll() is None: self.process.kill()
            self.process.wait(timeout=5)
            if self.process.stdin: self.process.stdin.close()
            self.process = None
        for handle in (getattr(self, "_monitor", None), getattr(self, "_error", None)):
            if handle: handle.close()

    def alive(self):
        return self.process is not None and self.process.poll() is None

    # ====== serial ======

    def log(self):
        return serial.SerialLog.from_file(self.serial)

    def wait(self, marker, timeout=40, occurrences=1, level=None):
        """Waits for a whole-line marker; stops at a terminal kernel state or a dead VM."""
        return self._wait(lambda log: log.count(marker, level) >= occurrences, marker, timeout)

    def wait_text(self, text, timeout=40, occurrences=1):
        """Prefix evidence such as 'PS2_KEY: ' that is not a marker line."""
        return self._wait(lambda log: log.text.count(text) >= occurrences, text, timeout)

    def wait_ready(self, timeout=90):
        """Waits for KERNEL_READY. The ordinary image ends in the shell: there it types exit first,
        so the BSP reaches the same halted state that the checks inspect."""
        log = self._wait(lambda log: log.count("KERNEL_READY", "OK") >= 1 or
            log.count("KERNEL_PS2_INPUT_READY", "OK") >= 1, "KERNEL_READY or shell", timeout)
        if log.count("KERNEL_READY", "OK") >= 1:
            return log
        for key in "exit":
            self.command("sendkey " + key + " 20")
            time.sleep(0.055)
        self.command("sendkey ret 20")
        return self.wait("KERNEL_READY", timeout)

    def _wait(self, ready, label, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            log = self.log()
            if log.terminal(): raise RuntimeError("Kernel failure: " + str(self.serial))
            if ready(log): return log
            if not self.alive(): raise RuntimeError("Owned VM exited before " + label)
            time.sleep(0.001)
        raise TimeoutError("Missing " + label + ": " + str(self.serial))

    # ====== monitor ======

    def command(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def screenshot(self, name, timeout=5):
        from PIL import Image
        target = self.directory / (name + ".ppm")
        self.command('screendump "' + target.as_posix() + '"')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.alive(): raise RuntimeError("VM exited during screenshot")
            try:
                with Image.open(target) as image:
                    image.load()
                    return image.convert("RGB")
            except (OSError, ValueError):
                time.sleep(0.005)
        raise TimeoutError("Incomplete screenshot")

    def physical(self, address, words=9, timeout=5):
        """Reads words 64-bit values of guest physical memory through the monitor (xp)."""
        offset = self.monitor.stat().st_size
        self.command(f"xp /{words}gx 0x{address:x}")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            values = {}
            text = self.monitor.read_bytes()[offset:].decode("utf-8", errors="replace")
            for match in re.finditer(r"(?m)^([0-9a-fA-F]{8,16}):\s*((?:0x[0-9a-fA-F]{16}\s*)+)", text):
                start = int(match[1], 16)
                for index, value in enumerate(re.findall(r"0x([0-9a-fA-F]{16})", match[2])):
                    values[start + index * 8] = int(value, 16)
            if all(address + index * 8 in values for index in range(words)):
                return [values[address + index * 8] for index in range(words)]
            if not self.alive(): raise RuntimeError("Owned VM exited before the physical read")
            time.sleep(0.005)
        raise TimeoutError("Physical memory read missing")


# ====== report_boots ======

def report_boots(results, passed, total):
    color = sys.stdout.isatty() and "NO_COLOR" not in os.environ
    for index, result in enumerate(results, 1):
        success = index <= passed and "Failure" not in result
        label = "[ Pass ]" if success else "[ Fail ]"
        if color:
            label = ("\033[32m" if success else "\033[31m") + label + "\033[0m"
        print(f"\n# ====== Boot {index} ====== #", flush=True)
        print(label, flush=True)
        if not success:
            cause = str(result.get("Failure", "Verifiche non superate; consultare le evidenze."))
            cause = " ".join(cause.split()) or "Errore senza messaggio; consultare le evidenze."
            print("Causa: " + cause, flush=True)
    if total > len(results):
        print(f"Boot non eseguiti: {total - len(results)}", flush=True)


# ====== finish ======

def finish(out, runner, image, results, passed, total):
    """Common end of a runner: results.json with one schema and a plain RESULT line."""
    report = dict(Runner=runner, Image=image_hash(image), Loader=image_hash(LOADER),
        Passed=passed, Total=total, Results=results)
    (Path(out) / "results.json").write_text(json.dumps(report, indent=2))
    report_boots(results, passed, total)
    print(f"RESULT {'PASS' if passed == total else 'FAIL'} {runner} {passed}/{total}", flush=True)
    print("Evidence: " + str(out), flush=True)
    return passed == total
