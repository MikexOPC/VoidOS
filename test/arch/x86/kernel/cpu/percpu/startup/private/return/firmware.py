"""Real IRETQ/NMI/IST proof on a SeaBIOS BSP, not native AP admission."""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("return_prefix", Path(__file__).parents[1] / "firmware.py")
private = importlib.util.module_from_spec(spec)
spec.loader.exec_module(private)
blob = private.blob
HIGH, NX = private.HIGH, private.NX


# ====== registers ======

def registers(vm):
    text = vm.command("human-monitor-command", {"command-line": "info registers"})
    values = {match[1]: int(match[2], 16) for match in re.finditer(r"\b(RIP|RSP|CR3|CR2|RFL)=([0-9a-f]+)", text, re.I)}
    return text, values


# ====== final_idt ======

def final_idt(nmi, fault):
    page = bytearray(4096)
    for vector in range(32):
        target = nmi if vector == 2 else fault
        ist = 2 if vector == 2 else 3 if vector == 8 else 1
        struct.pack_into("<HHBBHII", page, vector * 16, target & 65535, 8, ist, 0x8e,
            (target >> 16) & 65535, target >> 32, 0)
    return page


# ====== wait_iret_breakpoint ======

def wait_iret_breakpoint(vm, target):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        vm.command("stop")
        _, values = registers(vm)
        if values.get("RIP") == target:
            return values["RSP"]
        vm.command("cont")
        time.sleep(0.005)
    raise TimeoutError("IRET hardware breakpoint missing")


# ====== run_case ======

def run_case(out, packages, plan_file, code_address, case):
    data_address = code_address + 4096
    code = (packages / f"package-{code_address:x}-code.bin").read_bytes()
    data = (packages / f"package-{code_address:x}-data.bin").read_bytes()
    high = (out / "high.bin").read_bytes()
    target = (out / "target.bin").read_bytes()
    plan = bytearray(plan_file.read_bytes())
    if len(code) != 4096 or len(data) != 4096 or not 1 <= len(high) <= 4096 or len(plan) != 64 or len(target) > 4096:
        raise RuntimeError("Invalid return fixture sizes")
    iret, nmi, fault = struct.unpack_from("<QQQ", high, 5)
    stack, descriptors, local = struct.unpack_from("<QQQ", data, 56)
    frame = stack - 1024
    idt = final_idt(nmi, fault)
    extras = [(HIGH + 0x2000, target, (NX if case == "nx" else 0) | 1),
        (HIGH + 0x3000, idt, NX | 1), (HIGH + 0x4000, struct.pack("<HQ", 511, HIGH + 0x3000), NX | 1)]
    pages, backing = private.memory(code_address, data_address, data, high, split_ist=True, extra_pages=extras)
    if case == "plan":
        plan[8] ^= 1
    if case == "idt":
        pages[backing[HIGH + 0x3000]][2 * 16 + 4] = 0
    directory = out / f"{case}-{code_address:x}-{time.monotonic_ns()}"
    vm = blob.nmi.Experiment(directory, out / f"disk-{code_address:x}.img")
    try:
        vm.stage(1)
        trace_start = (directory / "cpu.log").stat().st_size
        for address, content in [(code_address, code), (data_address, data), *pages.items()]:
            blob.write(vm, address, content)
            if blob.read(vm, address, len(content)) != content:
                raise RuntimeError("Initial fixture readback differs")
        vm.permit(0x5008)
        vm.command("cont")
        blob.gate(vm, data_address, 1)
        vm.command("inject-nmi")
        vm.command("cont")
        private.wait_frame(vm, frame, 3)
        blob.write(vm, frame + 736, plan)
        blob.write(vm, local + 16, struct.pack("<II", 1, 0 if case == "inactive" else 2))
        if case == "tss":
            blob.write(vm, descriptors + 92, bytes(8))
        if case != "absent":
            vm.command("inject-nmi")
        if case == "iret-fault" and vm.remote(f"Z1,{iret:x},1") != "OK":
            raise RuntimeError("IRET breakpoint rejected")
        blob.write(vm, local + 280, struct.pack("<I", 1))
        vm.command("cont")
        if case == "iret-fault":
            operand = wait_iret_breakpoint(vm, iret)
            blob.write(vm, operand, struct.pack("<Q", 0x0000800000000000))
            if vm.remote(f"z1,{iret:x},1") != "OK":
                raise RuntimeError("IRET breakpoint removal failed")
            vm.command("cont")
        expected = 4 if case in ("positive", "absent") else 0xfffffffe if case in ("nx", "iret-fault") else 0xffffffff
        error = private.wait_frame(vm, frame, expected)
        if error != (0 if expected == 4 else 2 if expected == 0xfffffffe else 1):
            raise RuntimeError("Wrong private return terminal code")
        if case in ("nx", "iret-fault"):
            vm.command("cont")
            time.sleep(0.03)
            vm.command("stop")
        text, values = registers(vm)
        (directory / "registers.txt").write_text(text)
        count, nmi_rsp = struct.unpack("<QQ", blob.read(vm, local + 256, 16))
        wanted = 0 if case in ("absent", "plan", "idt", "tss", "inactive") else 1
        if count != wanted or (count and nmi_rsp != stack + 49152 - 40):
            raise RuntimeError(f"NMI count or IST2 differs: {count}/{nmi_rsp:x}")
        if values.get("CR3") != 0x20000 or private.physical_read(vm, 0x20000, 2048) != bytes(2048):
            raise RuntimeError("Operational root/low mapping differs")
        if blob.read(vm, frame, 104) != data[:104] or private.physical_read(vm, code_address, 4096) != code:
            raise RuntimeError("Entry record or low code changed")
        if expected != 0xffffffff and not re.search(rf"IDT=\s*{HIGH + 0x3000:016x}\s+000001ff", text, re.I):
            raise RuntimeError("Final IDT not active at IRET/fault")
        if expected == 4:
            if values.get("RSP") != frame or values.get("RFL", 0) & 0x7600:
                raise RuntimeError("IRET stack/flags differ")
            vm.command("inject-nmi")
            vm.command("cont")
            time.sleep(0.02)
            vm.command("stop")
            if struct.unpack("<Q", blob.read(vm, local + 256, 8))[0] != count + 1:
                raise RuntimeError("NMI still blocked after IRETQ")
        if expected == 0xfffffffe:
            fault_rsp = struct.unpack("<Q", blob.read(vm, local + 288, 8))[0]
            if fault_rsp != stack + 24576 - 48:
                raise RuntimeError("Fault did not use IST1")
        trace = (directory / "cpu.log").read_bytes()[trace_start:].decode(errors="replace")
        vector = "0e" if case == "nx" else "0d" if case == "iret-fault" else None
        if re.search("triple fault", trace, re.I) or (vector and not re.search(rf"v={vector}", trace, re.I)):
            raise RuntimeError("Unexpected triple fault or missing expected exception")
        if not vector and "check_exception" in trace:
            raise RuntimeError("Unexpected exception")
        return {"Case": case, "Code": hex(code_address), "NmiBeforePostcheck": count,
            "IretReached": expected != 0xffffffff, "Passed": True}
    except BaseException:
        text, _ = registers(vm)
        (directory / "failure-registers.txt").write_text(text)
        raise
    finally:
        vm.close()


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=10, choices=range(1, 21))
    args = parser.parse_args()
    rows = []
    try:
        for index in range(args.runs):
            rows.append(run_case(args.out, args.packages, args.plan, 0x8000 if index % 2 == 0 else 0xa000, "positive"))
            print(f"PASS: private IRETQ + pending NMI {index + 1}/{args.runs}", flush=True)
        for case in ("absent", "plan", "idt", "tss", "inactive", "nx", "iret-fault"):
            rows.append(run_case(args.out, args.packages, args.plan, 0x8000, case))
            print(f"PASS: private return control {case}", flush=True)
    finally:
        (args.out / "results.json").write_text(json.dumps({"Passed": len(rows), "Total": args.runs + 7,
            "Rows": rows, "KernelBoots": 0, "ApStarted": 0, "ModuleReady": False,
            "HighHash": hashlib.sha256((args.out / "high.bin").read_bytes()).hexdigest(),
            "PlanHash": hashlib.sha256(args.plan.read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
