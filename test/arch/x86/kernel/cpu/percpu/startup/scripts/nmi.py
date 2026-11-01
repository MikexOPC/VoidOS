"""Isolated SeaBIOS/TCG CPU experiment; never a kernel or AP acceptance gate."""
import argparse
import hashlib
import json
import re
import socket
import subprocess
import time
from pathlib import Path


# ====== free_ports ======

def free_ports():
    with socket.socket() as first, socket.socket() as second:
        first.bind(("127.0.0.1", 0))
        second.bind(("127.0.0.1", 0))
        return first.getsockname()[1], second.getsockname()[1]


# ====== connect ======

def connect(port, process):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Owned QEMU exited before connection")
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            time.sleep(0.01)
    raise TimeoutError("Owned QEMU endpoint missing")


# ====== Experiment ======

class Experiment:
    def __init__(self, directory, image):
        self.directory = directory
        directory.mkdir()
        self.qmp_socket = self.gdb = self.qmp_file = None
        self.process = None
        self.sequence = 0
        self.transcript = (directory / "protocol.log").open("w")
        self.errors = (directory / "stderr.log").open("w")
        qmp_port, gdb_port = free_ports()
        self.arguments = ["C:/Program Files/qemu/qemu-system-x86_64.exe", "-machine", "pc",
            "-accel", "tcg,thread=single", "-cpu", "max", "-m", "32", "-smp", "1",
            "-display", "none", "-serial", "none", "-monitor", "none", "-net", "none",
            "-no-reboot", "-S", "-boot", "c", "-drive",
            f"file={image.as_posix()},format=raw,if=ide,snapshot=on",
            "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
            "-gdb", f"tcp:127.0.0.1:{gdb_port}", "-d", "int,cpu_reset",
            "-D", (directory / "cpu.log").as_posix()]
        (directory / "arguments.json").write_text(json.dumps(self.arguments, indent=2))
        try:
            self.process = subprocess.Popen(self.arguments, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=self.errors,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.qmp_socket = connect(qmp_port, self.process)
            self.qmp_file = self.qmp_socket.makefile("rwb", buffering=0)
            greeting = self.receive()
            if "QMP" not in greeting:
                raise RuntimeError("Missing QMP greeting")
            self.command("qmp_capabilities")
            self.gdb = connect(gdb_port, self.process)
        except BaseException:
            self.close()
            raise

    # ====== receive ======

    def receive(self):
        line = self.qmp_file.readline()
        if not line:
            raise RuntimeError("QMP closed")
        value = json.loads(line)
        self.transcript.write("QMP " + json.dumps(value) + "\n")
        self.transcript.flush()
        return value

    # ====== command ======

    def command(self, name, arguments=None):
        self.sequence += 1
        request = {"execute": name, "id": self.sequence}
        if arguments is not None:
            request["arguments"] = arguments
        self.transcript.write("SEND " + json.dumps(request) + "\n")
        self.qmp_file.write(json.dumps(request).encode() + b"\r\n")
        for _ in range(64):
            response = self.receive()
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(str(response))
                return response["return"]
        raise RuntimeError("QMP event budget exceeded")

    # ====== remote ======

    def remote(self, text):
        payload = text.encode()
        self.gdb.sendall(b"$" + payload + b"#" + f"{sum(payload) & 255:02x}".encode())
        for _ in range(16):
            byte = self.gdb.recv(1)
            while byte in (b"+", b"-"):
                if byte == b"-":
                    raise RuntimeError("GDB rejected packet")
                byte = self.gdb.recv(1)
            if byte != b"$":
                raise RuntimeError("GDB framing")
            data = b""
            for _ in range(65536):
                byte = self.gdb.recv(1)
                if byte == b"#":
                    break
                if not byte:
                    raise RuntimeError("GDB closed")
                data += byte
            else:
                raise RuntimeError("GDB packet budget exceeded")
            checksum = self.gdb.recv(1) + self.gdb.recv(1)
            if int(checksum, 16) != sum(data) & 255:
                raise RuntimeError("GDB checksum")
            self.gdb.sendall(b"+")
            if data.startswith((b"T", b"S")):
                continue
            self.transcript.write(f"GDB {text}: {data.decode()}\n")
            self.transcript.flush()
            return data.decode()
        raise RuntimeError("GDB notification budget exceeded")

    # ====== words ======

    def words(self):
        value = bytes.fromhex(self.remote("m5000,10"))
        if len(value) != 16:
            raise RuntimeError("Incomplete guest state")
        return [int.from_bytes(value[i:i + 4], "little") for i in range(0, 16, 4)]

    # ====== permit ======

    def permit(self, address):
        if self.remote(f"M{address:x},4:01000000") != "OK":
            raise RuntimeError("Guest permission write failed")

    # ====== stage ======

    def stage(self, target):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            self.command("stop")
            words = self.words()
            if words[0] == target:
                registers = self.command("human-monitor-command", {"command-line": "info registers"})
                (self.directory / f"stage-{target}.txt").write_text(registers)
                return words
            if words[0] == 255 or self.process.poll() is not None:
                raise RuntimeError(f"Guest rejected CPU or exited: {words}")
            self.command("cont")
            time.sleep(0.01)
        raise TimeoutError(f"Guest stage {target} missing")

    # ====== close ======

    def close(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.kill()
            self.process.wait(timeout=5)
        for handle in (self.qmp_file, self.qmp_socket, self.gdb, self.transcript, self.errors):
            if handle is not None:
                handle.close()


# ====== run_case ======

def run_case(directory, image, second):
    vm = Experiment(directory, image)
    try:
        if vm.stage(1)[:2] != [1, 0]:
            raise RuntimeError("Initial IVT gate mismatch")
        trace = directory / "cpu.log"
        trace_start = trace.stat().st_size
        if second == "absent":
            vm.command("cont")
            time.sleep(0.05)
            vm.command("stop")
            if vm.words()[:2] != [1, 0]:
                raise RuntimeError("Guest transitioned without first NMI")
            return {"Case": second, "Passed": True, "State": vm.words()}
        vm.command("inject-nmi")
        vm.command("cont")
        if vm.stage(2)[:2] != [2, 1]:
            raise RuntimeError("Real NMI not entered")
        if second == "protected":
            vm.command("inject-nmi")
            vm.command("cont")
            time.sleep(0.05)
            vm.command("stop")
            if vm.words()[:2] != [2, 1]:
                raise RuntimeError("Second NMI escaped blocking in protected mode")
        vm.permit(0x5008)
        vm.command("cont")
        if vm.stage(3)[:2] != [3, 1]:
            raise RuntimeError("NMI blocking did not survive long-mode entry")
        registers = (directory / "stage-3.txt").read_text()
        if not re.search(r"CR3=0*10000\b", registers) or not re.search(r"EFER=0*500\b", registers):
            raise RuntimeError("Guest CR3/EFER mismatch")
        if not re.search(r"CS =0018[^\n]*CS64", registers) or not re.search(r"CR0=80010011\b", registers):
            raise RuntimeError("Guest did not reach intended 64-bit mode")
        if second == "long":
            vm.command("inject-nmi")
            vm.command("cont")
            time.sleep(0.05)
            vm.command("stop")
            if vm.words()[:2] != [3, 1]:
                raise RuntimeError("Second NMI escaped blocking before IRETQ")
        vm.permit(0x500c)
        vm.command("cont")
        state = vm.stage(5)
        if state[:2] != [5, 1 if second == "none" else 2]:
            raise RuntimeError(f"Wrong NMI count after IRETQ: {state}")
        guest_trace = trace.read_bytes()[trace_start:].decode(errors="replace")
        if re.search(r"triple fault|check_exception", guest_trace, re.IGNORECASE):
            raise RuntimeError("Unexpected guest fault in experiment")
        return {"Case": second, "Passed": True, "State": state}
    finally:
        vm.close()


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=10, choices=range(1, 21))
    args = parser.parse_args()
    results = []
    try:
        for index in range(args.runs):
            results.append(run_case(args.out / f"positive-{index:02}", args.image, "protected"))
            print(f"PASS NMI mode-transition experiment {index + 1}/{args.runs}", flush=True)
        for name in ("long", "none", "absent"):
            results.append(run_case(args.out / name, args.image, name))
    finally:
        (args.out / "results.json").write_text(json.dumps({"Results": results,
            "Passed": len(results), "Total": args.runs + 3, "KernelBoots": 0, "ApStarted": 0,
            "Firmware": "SeaBIOS", "ModuleReady": False,
            "ImageHash": hashlib.sha256(args.image.read_bytes()).hexdigest()}, indent=2))
    print("PASS: isolated NMI experiment; not an AP or OS boot gate", flush=True)


if __name__ == "__main__":
    main()
