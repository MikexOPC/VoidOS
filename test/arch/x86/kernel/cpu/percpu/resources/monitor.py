"""Independent AP arena paging, lease and inactive TSS checks; no AP execution claim."""

# ====== recursive ======

def recursive(a, b, c, d):
    return 0xffff000000000000 | a << 39 | b << 30 | c << 21 | d << 12


# ====== page_index ======

def page_index(count, page):
    cpu, local = divmod(page, 64)
    if not 1 <= cpu < count:
        return None
    offsets = dict(zip(range(1, 17), range(16)))
    offsets.update(zip(range(19, 23), range(16, 20)))
    offsets.update(zip(range(25, 29), range(20, 24)))
    offsets.update(zip(range(31, 35), range(24, 28)))
    offsets[48] = 28
    return (cpu - 1) * 29 + offsets[local] if local in offsets else None


# ====== verify ======

def verify(vm, log, memory, configured, ordinary, executing=0):
    if not 0 <= executing < configured:
        raise RuntimeError("Invalid explicit AP execution count")
    if log.count("KERNEL_AP_RESOURCES_PREPARED", "OK") != 1 or log.number("KERNEL_AP_CPUS") != configured:
        raise RuntimeError("AP resources missing or wrong CPU count")
    slot, nx = 487, 1 << 63
    base = (0xffff000000000000 | slot << 39) + 0x200000
    if log.number("KERNEL_AP_BASE") != base:
        raise RuntimeError("AP virtual base")
    root = memory(vm, recursive(510, 510, 510, 510) + slot * 8, 1)[0]
    data, pages, tables, count = (log.number("KERNEL_AP_" + name) for name in ("DATA", "DATA_PAGES", "TABLES", "TABLE_PAGES"))
    if configured == 1:
        if root or data or pages or tables or count:
            raise RuntimeError("AP resources allocated with only BSP")
        return dict(Aps=0, DataPages=0, Tables=0)
    if pages != 29 * (configured - 1) or count != 2 + (configured + 7) // 8 or (root & ~0x60) != tables | 3:
        raise RuntimeError("AP root/budget")
    if not ordinary and log.count("KERNEL_AP_ROLLBACK_OK", "OK") != 1:
        raise RuntimeError("AP diagnostic did not restore budget before repreparing")
    pdpt = memory(vm, recursive(510, 510, 510, slot), 512)
    pd = memory(vm, recursive(510, 510, slot, 0), 512)
    if any((entry & ~0x60) != ((tables + 4096) | 3 if i == 0 else 0) for i, entry in enumerate(pdpt)):
        raise RuntimeError("Unexpected AP PDPT entry")
    for i, entry in enumerate(pd):
        expected = (tables + (i + 1) * 4096) | 3 if 1 <= i <= count - 2 else 0
        if entry & ~0x60 != expected:
            raise RuntimeError("Unexpected AP PD entry")
    for pt in range(count - 2):
        entries = memory(vm, recursive(510, slot, 0, pt + 1), 512)
        for i, entry in enumerate(entries):
            physical = page_index(configured, pt * 512 + i)
            expected = 0 if physical is None else (data + physical * 4096) | nx | 3
            if entry & ~0x60 != expected:
                raise RuntimeError("AP leaf/guard/permissions mismatch")
    pool = log.number("KERNEL_AP_POOL")
    live = memory(vm, pool + 4144, 256)
    pins = memory(vm, pool + 6192, 32)
    for name, physical, length in (("DATA_ID", data, pages), ("TABLE_ID", tables, count)):
        ident = log.number("KERNEL_AP_" + name)
        matches = [i for i in range(64) if live[i * 4:i * 4 + 4] == [physical, length, ident, pool]]
        if len(matches) != 1 or (pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1:
            raise RuntimeError("AP frames not owned and pinned in actual allocator")
    for cpu in range(1, configured):
        unit = base + cpu * 0x40000
        meta = unit + 48 * 4096
        block = b"".join(v.to_bytes(8, "little") for v in memory(vm, meta, 22))
        if int.from_bytes(block[152:154], "little") != 39 or int.from_bytes(block[154:162], "little") != meta:
            raise RuntimeError("Inactive AP GDTR")
        if block[24] != 103 or block[29] != (0x8b if cpu <= executing else 0x89) or int.from_bytes(block[150:152], "little") != 104:
            raise RuntimeError("Inactive AP TSS descriptor or IO map")
        for offset, top in ((4, 17), (36, 23), (44, 29), (52, 35)):
            if int.from_bytes(block[48 + offset:56 + offset], "little") != unit + top * 4096:
                raise RuntimeError("AP RSP0/IST points to wrong stack")
        for start, end in ((1, 16), (19, 22), (25, 28), (31, 34)):
            if cpu <= executing:
                continue
            if memory(vm, unit + start * 4096, 1)[0] or memory(vm, unit + (end + 1) * 4096 - 8, 1)[0]:
                raise RuntimeError("AP stack not initially zero")
    return dict(Aps=configured - 1, DataPages=pages, Tables=count, Pinned=True)
