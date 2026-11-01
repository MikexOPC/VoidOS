"""Executes the packaged relocatable blob on one SeaBIOS BSP, not an AP/kernel gate."""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("blob_nmi_experiment", Path(__file__).parents[1] / "scripts/nmi.py")
nmi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nmi)
HIGH = 0xffffffff80100000


# ====== read ======

def read(vm, address, count):
    value = bytes.fromhex(vm.remote(f"m{address:x},{count:x}"))
    if len(value) != count:
        raise RuntimeError("Short guest read")
    return value


# ====== write ======

def write(vm, address, data):
    if vm.remote(f"M{address:x},{len(data):x}:{data.hex()}") != "OK":
        raise RuntimeError("Guest memory write failed")


# ====== gate ======

def gate(vm, data, expected):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        vm.command("stop")
        phase, error = struct.unpack("<II", read(vm, data + 104, 8))
        if phase == expected:
            return error
        if phase == 0xffffffff:
            raise RuntimeError(f"Blob failed before expected phase: {phase:x}/{error}")
        vm.command("cont")
        time.sleep(0.005)
    raise TimeoutError(f"Blob phase {expected:x} missing")


# ====== tables ======

def tables(code, data):
    pages = {base: bytearray(4096) for base in range(0x10000, 0x17000, 4096)}
    entries = [(0x10000, 0, 0x11003), (0x11000, 0, 0x12003), (0x12000, 0, 0x13003),
        (0x13000, code >> 12, code | 1), (0x13000, data >> 12, data | (1 << 63) | 3),
        (0x10000, (HIGH >> 39) & 511, 0x14003),
        (0x14000, (HIGH >> 30) & 511, 0x15003), (0x15000, (HIGH >> 21) & 511, 0x16003),
        (0x16000, (HIGH >> 12) & 511, 0x17001)]
    for base, index, value in entries:
        struct.pack_into("<Q", pages[base], index * 8, value)
    return pages


# ====== physical_word ======

def physical_word(vm, address):
    text = vm.command("human-monitor-command", {"command-line": f"xp /1gx 0x{address:x}"})
    values = re.findall(r"0x([0-9a-fA-F]{16})", text)
    if len(values) != 1:
        raise RuntimeError("Physical PTE missing")
    return int(values[0], 16)


# ====== run_case ======

def run_case(out, packages, physical_code, mode):
    physical_data = physical_code + 4096
    prefix = f"package-{physical_code:x}"
    code = (packages / f"{prefix}-code.bin").read_bytes()
    data = bytearray((packages / f"{prefix}-data.bin").read_bytes())
    high = (out / f"high-{1 if mode == 'nx' else 2 if mode == 'wp' else 0}.bin").read_bytes()
    if len(code) != 4096 or len(data) != 4096 or not 1 <= len(high) <= 4096:
        raise RuntimeError("Invalid package or destination size")
    if mode == "pat":
        data[89] ^= 1
    if mode == "id":
        struct.pack_into("<I", data, 12, 1)
    directory = out / f"{mode}-{physical_code:x}-{time.monotonic_ns()}"
    vm = nmi.Experiment(directory, out / f"disk-{physical_code:x}.img")
    try:
        vm.stage(1)
        trace = directory / "cpu.log"
        trace_start = trace.stat().st_size
        write(vm, physical_code, code)
        write(vm, physical_data, data)
        for base, page in tables(physical_code, physical_data).items():
            write(vm, base, page)
        write(vm, 0x17000, high + bytes(4096 - len(high)))
        if read(vm, physical_code, 4096) != code or read(vm, physical_data, 4096) != data:
            raise RuntimeError("Installed bytes differ from package")
        vm.permit(0x5008)
        vm.command("cont")
        if mode == "id":
            if gate(vm, physical_data, 0xffffffff) != 1:
                raise RuntimeError("Wrong identity failure")
        else:
            if gate(vm, physical_data, 1):
                raise RuntimeError("Error before IVT readiness")
            vm.command("inject-nmi")
            vm.command("cont")
            expected = 4 if mode == "positive" else 0xffffffff
            error = gate(vm, physical_data, expected)
            if error != (0 if mode == "positive" else 2 if mode == "pat" else 3):
                raise RuntimeError(f"Wrong terminal error: {error}")
        registers = vm.command("human-monitor-command", {"command-line": "info registers"})
        (directory / "registers.txt").write_text(registers)
        if read(vm, physical_code, 4096) != code or read(vm, physical_data, 104) != data[:104]:
            raise RuntimeError("Code or immutable entry record changed during execution")
        if mode in ("positive", "nx", "wp"):
            if not re.search(r"EFER=0*D00\b", registers, re.I) or not re.search(r"CS =0018[^\n]*CS64", registers):
                raise RuntimeError("Missing actual 64-bit mode/NXE")
            if not re.search(r"CR0=80010033\b", registers) or not re.search(r"CR3=0*10000\b", registers):
                raise RuntimeError("Control-register mismatch")
            code_pte = physical_word(vm, 0x13000 + (physical_code >> 12) * 8)
            data_pte = physical_word(vm, 0x13000 + (physical_data >> 12) * 8)
            if code_pte & ~0x60 != physical_code | 1 or data_pte & ~0x60 != physical_data | (1 << 63) | 3:
                raise RuntimeError("RO/X or RW/NX leaf mismatch")
        if mode == "positive":
            for register, value in (("RCX", physical_data), ("RDX", 520)):
                if not re.search(rf"{register}=0*{value:x}\b", registers, re.I):
                    raise RuntimeError("High entry argument mismatch")
            if not re.search(r"RIP=ffffffff801000[0-9a-f]{2}\b", registers, re.I):
                raise RuntimeError("High destination not executed")
            vm.command("inject-nmi")
            vm.command("cont")
            time.sleep(0.02)
            vm.command("stop")
            if struct.unpack("<II", read(vm, physical_data + 104, 8)) != (4, 0):
                raise RuntimeError("NMI escaped blocking before private entry/IRETQ")
        guest_trace = trace.read_bytes()[trace_start:].decode(errors="replace")
        if re.search("triple fault", guest_trace, re.I):
            raise RuntimeError("Unexpected triple fault")
        if mode in ("wp", "nx"):
            address = physical_code if mode == "wp" else physical_data
            if not re.search(rf"CR2=0*{address:x}\b", registers, re.I) or not re.search(r"v=0e", guest_trace, re.I):
                raise RuntimeError("Expected page fault not evidenced")
        elif "check_exception" in guest_trace:
            raise RuntimeError("Unexpected fault")
        return {"Passed": True, "Mode": mode, "Code": hex(physical_code), "Data": hex(physical_data)}
    finally:
        vm.close()


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=10, choices=range(1, 21))
    args = parser.parse_args()
    rows = []
    try:
        for index in range(args.runs):
            rows.append(run_case(args.out, args.packages, 0x8000 if index % 2 == 0 else 0xa000, "positive"))
            print(f"PASS: relocatable blob CPU {index + 1}/{args.runs}", flush=True)
        for mode in ("nx", "wp", "pat", "id"):
            rows.append(run_case(args.out, args.packages, 0x8000, mode))
            print(f"PASS: blob expected rejection {mode}", flush=True)
    finally:
        (args.out / "results.json").write_text(json.dumps({"Passed": len(rows), "Total": args.runs + 4,
            "Rows": rows, "KernelBoots": 0, "ApStarted": 0, "ModuleReady": False,
            "BlobTemplateHash": hashlib.sha256((args.packages / "blob.bin").read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
