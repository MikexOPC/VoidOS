"""K1: the kernel image window read from arch/x86/configs/layout.h, the only place that defines it."""
import re
from pathlib import Path

_HEADER = (Path(__file__).resolve().parents[2] / "arch/x86/configs/layout.h").read_text()


def _value(name):
    match = re.search(r"#define\s+" + name + r"\s+0x([0-9A-Fa-f]+)ULL", _HEADER)
    if not match:
        raise RuntimeError("layout.h: " + name + " missing")
    return int(match[1], 16)


KERNEL_IMAGE_BASE = _value("X86_KERNEL_IMAGE_BASE")
KERNEL_IMAGE_LIMIT = KERNEL_IMAGE_BASE + _value("X86_KERNEL_IMAGE_BYTES")


def slot(name):
    """PML4 index of a slot named in layout.h."""
    match = re.search(r"#define\s+" + name + r"\s+(\d+)U", _HEADER)
    if not match:
        raise RuntimeError("layout.h: " + name + " missing")
    return int(match[1])


def slot_address(name):
    """Base of a PML4 slot named in layout.h (Intel SDM Vol. 3A 3.3.7.1: upper half sign-extended)."""
    match = re.search(r"#define\s+" + name + r"\s+(\d+)U", _HEADER)
    if not match:
        raise RuntimeError("layout.h: " + name + " missing")
    slot = int(match[1])
    return (slot << 39) | (0xFFFF000000000000 if slot >= 256 else 0)
