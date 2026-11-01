"""Private prefix CPU fixture: BSP only, no kernel image or AP startup."""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("blob_fixture", Path(__file__).parents[1] / "blob/firmware.py")
blob = importlib.util.module_from_spec(spec)
spec.loader.exec_module(blob)
HIGH = blob.HIGH
NX = 1 << 63


# ====== memory ======

def memory(code, data, record, high, split_ist=False, extra_pages=()):
    pages = blob.tables(code, data)
    next_table, next_data = 0x30000, 0x50000
    backing = {}

    # ====== map_high ======

    def map_high(address, content, flags):
        nonlocal next_table, next_data
        table = 0x10000
        for shift in (39, 30, 21):
            index = ((address >> shift) & 511) * 8
            entry = struct.unpack_from("<Q", pages[table], index)[0]
            if not entry:
                entry = next_table | 3
                pages[next_table] = bytearray(4096)
                next_table += 4096
                struct.pack_into("<Q", pages[table], index, entry)
            table = entry & 0x000ffffffffff000
        physical = next_data
        next_data += 4096
        backing[address] = physical
        pages[physical] = bytearray(content) + bytes(4096 - len(content))
        struct.pack_into("<Q", pages[table], ((address >> 12) & 511) * 8, physical | flags)

    stack, descriptors, local = struct.unpack_from("<QQQ", record, 56)
    pages[0x17000] = bytearray(high) + bytes(4096 - len(high))
    for address in (stack - 8192, stack - 4096):
        map_high(address, b"", NX | 3)
    segment = bytearray(4096)
    struct.pack_into("<QQQ", segment, 0, 0, 0x00af9b000000ffff, 0x00cf93000000ffff)
    task = descriptors + 48
    tss = struct.pack("<H", 103) + task.to_bytes(8, "little")[:3] + bytes([0x89, 0, (task >> 24) & 255])
    segment[24:40] = tss + struct.pack("<II", task >> 32, 0)
    struct.pack_into("<Q", segment, 52, stack)
    for index, offset in enumerate((84, 92, 100), 1):
        top = stack + index * 24576 if split_ist else stack - 8192
        struct.pack_into("<Q", segment, offset, top)
        if split_ist:
            for address in range(top - 16384, top, 4096):
                map_high(address, b"", NX | 3)
    struct.pack_into("<H", segment, 150, 104)
    struct.pack_into("<HQ", segment, 152, 39, descriptors)
    map_high(descriptors, segment, NX | 3)
    map_high(local, struct.pack("<Q", local), NX | 3)
    for address, content, flags in extra_pages:
        map_high(address, content, flags)
    pages[0x20000] = bytearray(pages[0x10000])
    pages[0x20000][:2048] = bytes(2048)
    return pages, backing


# ====== physical_read ======

def physical_read(vm, address, length):
    if vm.remote("Qqemu.PhyMemMode:1") != "OK":
        raise RuntimeError("Physical debugger mode unavailable")
    try:
        return blob.read(vm, address, length)
    finally:
        if vm.remote("Qqemu.PhyMemMode:0") != "OK":
            raise RuntimeError("Virtual debugger mode restore failed")


# ====== wait_frame ======

def wait_frame(vm, frame, expected):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        vm.command("stop")
        response = vm.remote(f"m{frame + 664:x},8")
        if not response.startswith("E"):
            phase, error = struct.unpack("<II", bytes.fromhex(response))
            if phase == expected:
                return error
            if phase == 0xffffffff:
                raise RuntimeError(f"Unexpected private terminal {phase:x}/{error}")
        vm.command("cont")
        time.sleep(0.005)
    raise TimeoutError("Private frame phase missing")


# ====== run_case ======

def run_case(out, packages, code_address, mode):
    data_address = code_address + 4096
    code = (packages / f"package-{code_address:x}-code.bin").read_bytes()
    data = bytearray((packages / f"package-{code_address:x}-data.bin").read_bytes())
    high = (out / f"high-{mode}.bin").read_bytes()
    if len(code) != 4096 or len(data) != 4096 or len(high) > 4096:
        raise RuntimeError("Invalid fixture package size")
    stack, descriptors, local = struct.unpack_from("<QQQ", data, 56)
    frame = stack - 1024
    pages, backing = memory(code_address, data_address, data, high)
    if mode == "tss":
        pages[backing[descriptors]][29] = 0x80
    directory = out / f"{mode}-{code_address:x}-{time.monotonic_ns()}"
    vm = blob.nmi.Experiment(directory, out / f"disk-{code_address:x}.img")
    try:
        vm.stage(1)
        trace = directory / "cpu.log"
        trace_start = trace.stat().st_size
        for address, content in [(code_address, code), (data_address, data), *pages.items()]:
            blob.write(vm, address, content)
            if blob.read(vm, address, len(content)) != content:
                raise RuntimeError("Initial CPU fixture readback differs")
        vm.permit(0x5008)
        vm.command("cont")
        blob.gate(vm, data_address, 1)
        vm.command("inject-nmi")
        vm.command("cont")
        expected = 3 if mode == "positive" else 0xffffffff
        if wait_frame(vm, frame, expected) != (0 if mode == "positive" else 1):
            raise RuntimeError("Wrong private terminal error")
        registers = vm.command("human-monitor-command", {"command-line": "info registers"})
        (directory / "registers.txt").write_text(registers)
        for pattern in (r"CR3=0*20000\b", r"CS =0008[^\n]*CS64", r"SS =0010\b"):
            if not re.search(pattern, registers, re.I):
                raise RuntimeError(f"Private context missing: {pattern}")
        copied = blob.read(vm, frame, 104)
        if copied != data[:104] or physical_read(vm, code_address, 4096) != code:
            raise RuntimeError("Copied record or RO code differs")
        if physical_read(vm, 0x20000, 2048) != bytes(2048):
            raise RuntimeError("Operational root retains low mappings")
        if mode == "positive":
            for pattern in (rf"GS =0000 {local:016x}\b", rf"TR =0018 {descriptors + 48:016x}\b",
                rf"GDT=\s*{descriptors:016x}\s+00000027", rf"IDT=\s*{frame + 128:016x}\s+000001ff"):
                if not re.search(pattern, registers, re.I):
                    raise RuntimeError(f"Private descriptor/GS missing: {pattern}")
            if blob.read(vm, descriptors + 29, 1) != b"\x8b":
                raise RuntimeError("LTR did not set private TSS busy")
            fx = blob.read(vm, frame - 512, 512)
            if struct.unpack_from("<H", fx)[0] != 0x37f or fx[4] != 0 or struct.unpack_from("<I", fx, 24)[0] != 0x1f80:
                raise RuntimeError("x87/MXCSR initialization differs")
            if fx[160:416] != bytes(256):
                raise RuntimeError("Initial XMM registers are not zero")
            idt = blob.read(vm, frame + 128, 512)
            for offset in range(0, 512, 16):
                if idt[offset + 2:offset + 6] != b"\x08\x00\x00\x8e" or idt[offset + 12:offset + 16] != bytes(4):
                    raise RuntimeError("High emergency gate differs")
            vm.command("inject-nmi")
            vm.command("cont")
            time.sleep(0.02)
            vm.command("stop")
            if blob.read(vm, frame + 664, 8) != struct.pack("<II", 3, 0):
                raise RuntimeError("NMI escaped before final admission/IRETQ")
        guest_trace = trace.read_bytes()[trace_start:].decode(errors="replace")
        if re.search("triple fault", guest_trace, re.I):
            raise RuntimeError("Unexpected triple fault")
        if mode != "positive":
            vector = "0d" if mode == "tss" else "0e"
            if not re.search(rf"v={vector}", guest_trace, re.I):
                raise RuntimeError("Expected fault not evidenced")
            if mode != "tss":
                address = data_address if mode == "low" else stack - 65536 - 4096
                if not re.search(rf"CR2=0*{address:x}\b", registers, re.I):
                    raise RuntimeError("Wrong expected page fault address")
        elif "check_exception" in guest_trace:
            raise RuntimeError("Unexpected exception")
        return {"Mode": mode, "Code": hex(code_address), "Frame": hex(frame), "Passed": True}
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
            print(f"PASS: private prefix CPU {index + 1}/{args.runs}", flush=True)
        for mode in ("low", "guard", "tss"):
            rows.append(run_case(args.out, args.packages, 0x8000, mode))
            print(f"PASS: private prefix expected fault {mode}", flush=True)
    finally:
        (args.out / "results.json").write_text(json.dumps({"Passed": len(rows), "Total": args.runs + 3,
            "Rows": rows, "KernelBoots": 0, "ApStarted": 0, "ModuleReady": False,
            "HighHash": hashlib.sha256((args.out / "high-positive.bin").read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
