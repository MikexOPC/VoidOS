# ====== verify_concurrent ======

def verify(vm, log, count, root, pool, free, first, memory):
    if count < 3:
        raise RuntimeError("Concurrent profile requires two started APs")
    addresses = [log.number("KERNEL_AP_BATCH_RECORD"), log.number("KERNEL_AP_SECOND_RECORD")]
    records = [memory(vm, address, 48) for address in addresses]
    shared = records[0]
    if (shared[:3] != [addresses[0], root, pool] or shared[3] < 64 or
            shared[4] != shared[3] - 64 or shared[5] != shared[3] or free != shared[5] or
            shared[6:18] != [16, 16, 16, 16, 136, 272, 408, 544, 17, 34, 51, 68] or
            not all(shared[23:27]) or shared[38:40] != [4, 4] or any(shared[44:])):
        raise RuntimeError("Concurrent task progress or retained BSP cleanup differs")
    bases = shared[29:33]
    if (any(bases[i] != bases[0] + i * 18 * 4096 for i in range(4)) or
            any(memory(vm, log.number("KERNEL_AP_BATCH_PTE"), 72)) or
            len(set(shared[34:38])) != 4 or
            any(address & 15 or not 0xffffffff80000000 <= address < 0xffffffff80400000 - 576
                for address in shared[34:38])):
        raise RuntimeError("Concurrent stack geometry, contexts or revocation differs")
    preempt_addresses = [log.number("KERNEL_AP_PREEMPT_RECORD"), log.number("KERNEL_AP_SECOND_PREEMPT")]
    if len(set(addresses + preempt_addresses)) != 4:
        raise RuntimeError("Concurrent evidence records overlap")
    for slot, record in enumerate(records):
        lower = slot * 2
        if (record[18] < 2 or not record[19] or record[19] & ~(3 << lower) or
                not lower <= record[21] < lower + 2 or
                record[22] != shared[23 + record[21]] or not 8 <= record[27] <= 8192 or
                record[28] != 2 or record[40] & 0x7602 != 0x202 or record[41:44] != [0xe1, 1, 0]):
            raise RuntimeError("Concurrent IRQ identity or dispatcher counters differ")
        if not bases[record[21]] + 4096 <= record[20] < bases[record[21]] + 65536:
            raise RuntimeError("Concurrent IRQ frame escaped its owned stack")
        local = memory(vm, record[33], 88)
        if (record[33] & 63 or not 0xffffffff80000000 <= record[33] < 0xffffffff80400000 - 704 or
                local[74:78] != [record[27] | 2 << 32, 0, 0, 0] or
                local[78:82] != [0, 0, 0xffffffff00000000, 0] or
                local[82:84] != [record[33], first + slot * 448]):
            raise RuntimeError("Concurrent dispatcher did not quiesce on its private CPU")
        address = preempt_addresses[slot]
        preempt = memory(vm, address, 16)
        if (address & 63 or preempt[:7] != [address, 3, 3, 3, 3, 6, 1] or preempt[7] > 8 or
                not 0 < preempt[14] <= 0xffffffff or preempt[15]):
            raise RuntimeError("Concurrent timer preemption or stop differs")
        for task in range(2):
            base = bases[lower + task]
            if (not base + 4096 <= preempt[8 + task] < base + 65536 - 48 or
                    not base + 4096 <= preempt[10 + task] < base + 65536 or
                    not 0xffffffff80000000 <= preempt[12 + task] < 0xffffffff80400000):
                raise RuntimeError("Concurrent preemption frame ownership differs")
    if records[0][33] == records[1][33]:
        raise RuntimeError("Concurrent dispatchers share native context")
    if (memory(vm, log.number("KERNEL_AP_LOT_RECORD"), 7) != [0, root, 1, 6, 6, 6, 6] or
            memory(vm, log.number("KERNEL_AP_LOT_ARRIVED"), 2) != [1, 1]):
        raise RuntimeError("Global completion or workload overlap barrier differs")
