"""K3-H: physical inactive tables and leases; BSP CR3 unchanged, APs stopped."""
import argparse
import importlib.util
import re
import hashlib
import struct
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[7]


# ====== load ======

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cpu = load("transition_cpu", Path(__file__).parents[1] / "firmware.py")
low = load("transition_low", Path(__file__).parents[1] / "lowmemory/firmware.py")
qemu = cpu.qemu


# ====== registers ======

def registers(vm):
    def ready(text):
        rax = re.search(r"\bRAX=([0-9a-fA-F]+)", text)
        cr3 = re.search(r"\bCR3=([0-9a-fA-F]+)", text)
        if rax and cr3 and "HLT=" in text:
            return int(rax[1], 16), int(cr3[1], 16), "HLT=1" in text
        return None
    return cpu.monitor_read(vm, "info registers", ready)


# ====== verify ======

def verify(vm, log, rollback, freeze=False, image=False, erased=False, joint=False, failure=False):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        rax, actual, halted = registers(vm)
        if halted and rax == (0x4b50414e49430001 if failure else 0x5452414e53505245):
            break
        time.sleep(0.01)
    else:
        raise RuntimeError("Missing actual diagnostic HLT marker")
    if log.failures() != (["AP_JOINT_PRIVATE_SEAL_FAILED"] if failure else []) or log.count("KERNEL_AP_TRANSITION_PREPARED", "OK") != 1:
        raise RuntimeError("Unexpected fault or missing preparation")
    if log.count("KERNEL_AP_TRANSITION_ROLLBACK_OK", "OK") != int(rollback):
        raise RuntimeError("Rollback marker mismatch")
    code = low.verify(vm, log, False, ("AP_JOINT_PRIVATE_SEAL_FAILED",) if failure else ())["Base"]
    code = int(code, 16)
    address, tables, data, root, snapshot = (log.number("KERNEL_AP_TRANSITION_" + key)
        for key in ("OWNER", "TABLES", "DATA", "ROOT", "SOURCE"))
    if actual != root or tables == root or tables & 4095 or not 4096 <= tables <= 0xffffc000:
        raise RuntimeError("CR3 changed or bootstrap root not below 4 GiB")
    if not 4096 <= data <= 0xff000 or data & 4095 or data == code:
        raise RuntimeError("Invalid separate low data page")
    if not 0xffffffff80000000 <= address < 0xffffffff80400000 - 8344 or snapshot != address + 152:
        raise RuntimeError("Owner outside static image or wrong snapshot geometry")
    owner = cpu.memory(vm, address, 19)
    pool = log.number("KERNEL_AP_LOW_POOL")
    if owner[:5] != [address, pool, log.number("KERNEL_AP_LOW_OWNER"), data, 1] or owner[6:9] != [pool, tables, 4]:
        raise RuntimeError("Owner lease metadata mismatch")
    if owner[10] != pool or owner[11] != root or owner[17:] != [(4 if freeze or joint else 3) | (1 << 32), 1]:
        raise RuntimeError("Owner not prepared/pinned or reserved metadata changed")
    if log.count("KERNEL_AP_TRANSITION_FROZEN_RETAINED", "OK") != int(freeze):
        raise RuntimeError("Freeze marker mismatch")
    source = cpu.memory(vm, snapshot, 512)
    active = vm.physical(root, 512)
    if any(active[:256]) or any((a & ~0x60) != (b & ~0x60) for a, b in zip(active, source)):
        raise RuntimeError("Operational PML4 changed during inactive preparation")
    if source[510] & ~0x60 != root | (1 << 63) | 3:
        raise RuntimeError("Source recursive entry")
    for table in range(4):
        page = vm.physical(tables + table * 4096, 512)
        expected = [0] * 512
        if table == 0:
            expected[0] = tables + 4096 | 3
            expected[256:] = source[256:]
            expected[510] = 0
        elif table < 3:
            expected[0] = tables + (table + 1) * 4096 | 3
        else:
            expected[code >> 12] = code | 1
            expected[data >> 12] = data | (1 << 63) | 3
        if page != expected:
            raise RuntimeError(f"Inactive table {table}: extra mapping, wrong physical target or permissions")
    if image or erased:
        if log.count("KERNEL_AP_IMAGE_INSTALLED_INACTIVE", "OK") != 1 or log.count(
                "KERNEL_AP_IMAGE_ERASED_VERIFIED", "OK") != int(erased):
            raise RuntimeError("Installation/erase marker mismatch")
        installed = log.number("KERNEL_AP_IMAGE_OWNER")
        if not 0xffffffff80000000 <= installed <= 0xffffffff80400000 - 12424:
            raise RuntimeError("Installation owner outside static image")
        actual_code, actual_data = vm.physical(code, 512), vm.physical(data, 512)
        if erased:
            if any(actual_code) or any(actual_data) or any(cpu.memory(vm, installed, 1553)):
                raise RuntimeError("Erase did not clear both pages and owner")
        else:
            expected_code, expected_data = (log.number("KERNEL_AP_IMAGE_" + key + "_EXPECTED") for key in ("CODE", "DATA"))
            if expected_code != installed + 136 or expected_data != installed + 4232:
                raise RuntimeError("Expected image geometry differs")
            if actual_code != cpu.memory(vm, expected_code, 512) or actual_data != cpu.memory(vm, expected_data, 512):
                raise RuntimeError("Physical code/data readback differs from C/Rust image")
            metadata = cpu.memory(vm, installed, 17)
            if metadata[0] != installed or metadata[1] != address or metadata[16] != (4 if joint else 2):
                raise RuntimeError("Installation owner identity/state differs")
            raw = b"".join(word.to_bytes(8, "little") for word in actual_data)
            record = struct.unpack_from("<IIII11Q", raw)
            identities = cpu.memory(vm, log.number("KERNEL_CPU_IDENTITIES") + 48, 2)
            if record[:4] != (1, 104, 1, identities[0] >> 32) or record[4:9] != (520, code, data, tables, root):
                raise RuntimeError("Installed record is not bound to actual leases/CPU")
            if record[11] != log.number("KERNEL_AP_CONTEXTS") or record[14]:
                raise RuntimeError("Installed GS pointer/reserved field differs")
            if struct.unpack_from("<II", raw, 104) != (0, 0):
                raise RuntimeError("Blob executed despite inactive diagnostic")
            (vm.directory / "installed-code.sha256").write_text(hashlib.sha256(
                b"".join(word.to_bytes(8, "little") for word in actual_code)).hexdigest())
    elif any(vm.physical(data, 512)):
        raise RuntimeError("Bootstrap data not initialized")
    header = cpu.memory(vm, pool, 6)
    live = cpu.memory(vm, pool + 4144, 256)
    pins = cpu.memory(vm, pool + 6192, 32)
    if header[2] != owner[13] - 4:
        raise RuntimeError("Table budget mismatch")
    for base, count, ident in ((data, 1, owner[5]), (tables, 4, owner[9])):
        matches = [i for i in range(64) if live[i * 4:i * 4 + 4] == [base, count, ident, pool]]
        if len(matches) != 1 or (pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1:
            raise RuntimeError("Transition frame lease not unique/pinned")
    return dict(OsCpusReady=1, Tables=hex(tables), RootUnchanged=hex(root), Data=hex(data),
                Code=hex(code), LowLeaves=2, TablePages=4, Rollback=rollback, Pinned=True, Frozen=freeze or joint,
                ImageInstalled=image, ImageErased=erased)


# ====== main ======

# ====== wait_ready ======

def wait_ready(vm):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = vm.log()
        if log.failures() or log.terminal():
            raise RuntimeError("Guest failure before transition checkpoint: " + str(log.failures()))
        if log.count("KERNEL_AP_TRANSITION_TEST_READY", "OK") == 1:
            return log
        if not vm.alive():
            raise RuntimeError("Owned VM exited before transition checkpoint")
        time.sleep(0.002)
    raise TimeoutError("Transition checkpoint deadline exceeded")


# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 21))
    parser.add_argument("--rollback", action="store_true")
    parser.add_argument("--freeze", action="store_true")
    parser.add_argument("--image-installed", action="store_true")
    parser.add_argument("--image-erased", action="store_true")
    args = parser.parse_args()
    if sum((args.rollback, args.freeze, args.image_installed, args.image_erased)) > 1:
        parser.error("Diagnostic modes require separate images")
    out = ROOT / "kernel/build" / ("ap-transition-firmware-" + uuid.uuid4().hex)
    out.mkdir()
    rows = []
    try:
        for index in range(1, args.boots + 1):
            with qemu.Vm(out / f"boot-{index}", args.image, "std", smp=4) as vm:
                try:
                    log = wait_ready(vm)
                except Exception:
                    import json
                    try:
                        state = registers(vm)
                        (vm.directory / "failure-state.json").write_text(json.dumps(state))
                        (vm.directory / "failure-pml4.json").write_text(json.dumps(vm.physical(state[1], 512)))
                    except Exception as detail:
                        (vm.directory / "failure-capture.txt").write_text(str(detail))
                    raise
                rows.append(verify(vm, log, args.rollback, args.freeze, args.image_installed, args.image_erased))
                print(f"PASS: inactive AP root {index}/{args.boots}; physical tables, permissions, ownership, BSP CR3", flush=True)
    except Exception as failure:
        rows.append(dict(Failure=str(failure)))
        qemu.finish(out, "ap-transition", args.image, rows, len(rows) - 1, args.boots)
        raise
    if not qemu.finish(out, "ap-transition", args.image, rows, len(rows), args.boots):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
