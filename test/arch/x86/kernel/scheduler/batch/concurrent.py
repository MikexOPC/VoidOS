# ====== verify_concurrent ======

def verify(vm, log, count, root, pool, free, first, memory, target=3, rounds=1, tasks=4):
    aps = tasks // 2
    mask = (1 << (aps + 1)) - 2
    irq, gen = 6 + 3 * tasks, 11 + 3 * tasks
    steps, bases_at = gen + tasks, gen + tasks + 2
    local_at = bases_at + tasks
    retained = local_at + tasks + 1
    if tasks not in (4, 8) or count <= aps:
        raise RuntimeError("Concurrent profile requires all participating APs")
    addresses = (log.numbers("KERNEL_AP_CPU_RECORD") if tasks == 8 else
                 [log.number("KERNEL_AP_BATCH_RECORD"), log.number("KERNEL_AP_SECOND_RECORD")])
    if len(addresses) != aps or len(set(addresses)) != aps:
        raise RuntimeError("Concurrent evidence CPU count differs")
    records = [memory(vm, address, 24 + 6 * tasks) for address in addresses]
    shared = records[0]
    if (shared[:3] != [addresses[0], root, pool] or shared[3] < 16 * tasks or
            shared[4] != shared[3] - 16 * tasks or shared[5] != shared[3] or free != shared[5] or
            shared[6:irq] != [16] * tasks + [136 * (i + 1) for i in range(tasks)] + [17 * (i + 1) for i in range(tasks)] or
            not all(shared[gen:gen + tasks]) or shared[retained:retained + 2] != [tasks, tasks] or any(shared[retained + 6:])):
        raise RuntimeError("Concurrent task progress or retained BSP cleanup differs")
    bases = shared[bases_at:bases_at + tasks]
    if (any(bases[i] != bases[0] + i * 18 * 4096 for i in range(tasks)) or
            any(memory(vm, log.number("KERNEL_AP_BATCH_PTE"), 18 * tasks)) or
            len(set(shared[local_at + 1:local_at + 1 + tasks])) != tasks or
            any(address & 15 or not 0xffffffff80000000 <= address < 0xffffffff80400000 - 576
                for address in shared[local_at + 1:local_at + 1 + tasks])):
        raise RuntimeError("Concurrent stack geometry, contexts or revocation differs")
    preempt_addresses = (log.numbers("KERNEL_AP_CPU_PREEMPT") if tasks == 8 else
                         [log.number("KERNEL_AP_PREEMPT_RECORD"), log.number("KERNEL_AP_SECOND_PREEMPT")])
    if len(preempt_addresses) != aps or len(set(addresses + preempt_addresses)) != 2 * aps:
        raise RuntimeError("Concurrent evidence records overlap")
    for slot, record in enumerate(records):
        lower = slot * 2
        if (record[irq] < 2 * rounds or not record[irq + 1] or record[irq + 1] & ~(3 << lower) or
                not lower <= record[irq + 3] < lower + 2 or
                record[irq + 4] != shared[gen + record[irq + 3]] or not 2 * target + 2 <= record[steps] <= 8192 or
                record[steps + 1] != 2 or record[retained + 2] & 0x7602 != 0x202 or record[retained + 3:retained + 6] != [0xe1, 1, 0]):
            raise RuntimeError("Concurrent IRQ identity or dispatcher counters differ")
        if not bases[record[irq + 3]] + 4096 <= record[irq + 2] < bases[record[irq + 3]] + 65536:
            raise RuntimeError("Concurrent IRQ frame escaped its owned stack")
        local = memory(vm, record[local_at], 88)
        if (record[local_at] & 63 or not 0xffffffff80000000 <= record[local_at] < 0xffffffff80400000 - 704 or
                local[74:78] != [record[steps] | 2 << 32, 0, 0, 0] or
                local[78:82] != [0, 0, 0xffffffff00000000, 0] or
                local[82:84] != [record[local_at], first + slot * 448]):
            raise RuntimeError("Concurrent dispatcher did not quiesce on its private CPU")
        address = preempt_addresses[slot]
        preempt = memory(vm, address, 16)
        if (address & 63 or preempt[:7] != [address, target, target, target, target, 2 * target, 1] or preempt[7] > 8 or
                not 0 < preempt[14] <= 0xffffffff or preempt[15]):
            raise RuntimeError("Concurrent timer preemption or stop differs")
        for task in range(2):
            base = bases[lower + task]
            if (not base + 4096 <= preempt[8 + task] < base + 65536 - 48 or
                    not base + 4096 <= preempt[10 + task] < base + 65536 or
                    not 0xffffffff80000000 <= preempt[12 + task] < 0xffffffff80400000):
                raise RuntimeError("Concurrent preemption frame ownership differs")
    if len({record[local_at] for record in records}) != aps:
        raise RuntimeError("Concurrent dispatchers share native context")
    if (memory(vm, log.number("KERNEL_AP_LOT_RECORD"), 7) != [0, root, 1, mask, mask, mask, mask] or
            memory(vm, log.number("KERNEL_AP_LOT_ARRIVED"), aps) != [1] * aps):
        raise RuntimeError("Global completion or workload overlap barrier differs")


# ====== verify_incomplete ======

def verify_incomplete(vm, log, root, pool, free, live, pins, memory, tasks=4):
    aps = tasks // 2
    mask = (1 << (aps + 1)) - 2
    irq = 6 + 3 * tasks
    completed, retained = 12 + 4 * tasks, 14 + 6 * tasks
    address = log.number("KERNEL_AP_BATCH_RECORD")
    record = memory(vm, address, 24 + 6 * tasks)
    if (not log.before("KERNEL_AP_LOT_INCOMPLETE", "KERNEL_SMP_PANIC") or
            record[:3] != [address, root, pool] or record[3] < 16 * tasks or
            record[4] != record[3] - 16 * tasks or record[5] != 0 or free != record[4] or
            record[6:irq] != [16] * tasks + [136 * (i + 1) for i in range(tasks)] + [17 * (i + 1) for i in range(tasks)] or
            record[completed] != 2 or record[retained:retained + 2] != [0, 0] or any(record[retained + 6:]) or
            memory(vm, log.number("KERNEL_AP_LOT_RECORD"), 7) != [0, root, 1, mask, mask, mask, mask & ~(1 << aps)]):
        raise RuntimeError("Incomplete quiescence did not retain the complete task batch")
    entries = memory(vm, log.number("KERNEL_AP_BATCH_PTE"), 18 * tasks)
    frames = []
    for task in range(tasks):
        leaves = entries[task * 18:(task + 1) * 18]
        frame = leaves[1] & 0x000ffffffffff000
        if (leaves[0] or leaves[17] or not frame or
                any((leaves[page + 1] & ~0x60) != ((frame + page * 4096) | 3 | (1 << 63))
                    for page in range(16))):
            raise RuntimeError("Retained task mapping or guard changed before panic")
        matches = [i for i in range(len(live) // 4) if live[4 * i] == frame and live[4 * i + 2]]
        if (len(matches) != 1 or live[4 * matches[0] + 1] != 16 or
                (pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1):
            raise RuntimeError("Retained task lease is missing or unpinned")
        frames.append(frame)
    frames.sort()
    if any(frames[i] + 65536 > frames[i + 1] for i in range(tasks - 1)):
        raise RuntimeError("Retained task frames overlap")
