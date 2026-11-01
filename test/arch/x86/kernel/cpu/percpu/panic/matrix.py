"""Build-independent variants: patch only the diagnostic data word, never executable bytes."""
import argparse
import importlib.util
import json
import re
import struct
import uuid
from pathlib import Path
spec = importlib.util.spec_from_file_location("panic_firmware", Path(__file__).with_name("firmware.py"))
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)
CASES = {"prepare": 1, "voluntary": 2, "opcode": 3, "page": 4, "nmi": 5,
         "double": 6, "no-simd": 7, "recursive": 8, "lock-held": 9, "prepare-rollback": 10, "no-simd-call": 11}

# ====== data_offset ======

def data_offset(image, symbols):
    matches = re.findall(r"\bxp_panic_test_mode\s+([0-9a-fA-F]{16})\b", symbols)
    if len(matches) != 1: raise RuntimeError("Diagnostic selector symbol not unique")
    address = int(matches[0], 16)
    if image[:6] != b"\x7fELF\x02\x01": raise RuntimeError("Expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", image, 32)[0]
    stride, count = struct.unpack_from("<HH", image, 54)
    if stride != 56: raise RuntimeError("Unexpected program header size")
    offsets = []
    for index in range(count):
        kind, flags, offset, virtual, _, size, _, _ = struct.unpack_from("<IIQQQQQQ", image, phoff + index * stride)
        if kind == 1 and flags & 2 and not flags & 1 and virtual <= address <= virtual + size - 8:
            offsets.append(offset + address - virtual)
    if len(offsets) != 1 or offsets[0] & 7: raise RuntimeError("Selector is not in one aligned writable non-code segment")
    if struct.unpack_from("<Q", image, offsets[0])[0] != 1: raise RuntimeError("Base selector must be prepare")
    return address, offsets[0]

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--boots", type=int, default=10, choices=range(1, 11))
    args = parser.parse_args()
    base = args.image.read_bytes()
    address, offset = data_offset(base, (args.map or args.image.with_suffix(".map")).read_text())
    out = firmware.ROOT / "kernel/build" / ("panic-matrix-" + uuid.uuid4().hex)
    out.mkdir(); results = []
    try:
        for case, code in CASES.items():
            image = bytearray(base)
            struct.pack_into("<Q", image, offset, code)
            if image[:offset] != base[:offset] or image[offset + 8:] != base[offset + 8:]:
                raise RuntimeError("Unexpected modification outside the selector")
            path = out / (case + ".elf"); path.write_bytes(image)
            count = args.boots if case == "prepare" else 1
            for iteration in range(1, count + 1):
                row = firmware.boot(path, out / case, iteration, case, 4)
                row["Image"] = firmware.qemu.image_hash(path)
                results.append(row)
                print(f"PASS: panic matrix {case} {iteration}/{count}", flush=True)
        row = firmware.boot(out / "prepare.elf", out / "bsp-only", 1, "prepare", 1)
        row["Image"] = firmware.qemu.image_hash(out / "prepare.elf")
        results.append(row)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        (out / "results.json").write_text(json.dumps(dict(Passed=False, Results=results), indent=2))
        print("Evidence: " + str(out), flush=True)
        raise
    total = args.boots + len(CASES)
    (out / "results.json").write_text(json.dumps(dict(Passed=len(results) == total, Total=total,
        BaseImage=firmware.qemu.image_hash(args.image), Selector=hex(address), FileOffset=offset,
        Loader=firmware.qemu.image_hash(firmware.qemu.LOADER), Results=results), indent=2))
    print(f"RESULT PASS panic-matrix {len(results)}/{total}\nEvidence: {out}", flush=True)

if __name__ == "__main__": main()
