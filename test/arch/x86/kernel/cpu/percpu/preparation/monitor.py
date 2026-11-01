"""Inspect inactive AP contexts; never counts them as executing CPUs."""


# ====== verify ======

def verify(vm, log, memory, configured, ordinary, committed):
    if configured == 1:
        if "KERNEL_AP_CONTEXTS_PREPARED" in log.text or "KERNEL_AP_COMMIT_RETAINED" in log.text:
            raise RuntimeError("AP context publication on BSP-only profile")
        return dict(Prepared=0, Executing=0, Committed=False)
    first = log.number("KERNEL_AP_CONTEXTS")
    count = log.number("KERNEL_AP_CONTEXT_COUNT")
    if count != configured - 1 or first & 63 or not 0xffffffff80000000 <= first < 0xffffffff80400000 - 63 * 448:
        raise RuntimeError("AP context header or owner range")
    if log.count("KERNEL_AP_CONTEXTS_PREPARED", "OK") != 1:
        raise RuntimeError("Missing AP contexts marker")
    if not ordinary and log.count("KERNEL_AP_CONTEXT_ROLLBACK_OK", "OK") != 1:
        raise RuntimeError("Missing AP context rollback evidence")
    if ordinary and "KERNEL_AP_CONTEXT_ROLLBACK_OK" in log.text:
        raise RuntimeError("Diagnostic AP rollback in ordinary image")
    if log.count("KERNEL_AP_COMMIT_RETAINED", "OK") != int(committed):
        raise RuntimeError("AP resource commit profile mismatch")
    bsp_wait = log.number("KERNEL_PERCPU_WAIT")
    reference = memory(vm, bsp_wait, 8)
    for index in range(1, configured):
        address = first + (index - 1) * 448
        owner = memory(vm, address, 56)
        wait = bsp_wait + index * 64
        if owner[:7] != [address, 0, index | (1 << 32), address + 24, 1, wait, 0] or any(owner[7:]):
            raise RuntimeError("AP context is not private, prepared and quiescent")
        slot = memory(vm, wait, 8)
        if slot != [reference[0], wait + 32, reference[2], reference[3],
                    reference[4], reference[5], reference[6], address + 24]:
            raise RuntimeError("AP clock source or lifetime mismatch")
    return dict(Prepared=count, Executing=0, Committed=committed)
