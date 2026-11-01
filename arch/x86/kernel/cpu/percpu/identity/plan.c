#include "identity.h"
#include "../internal.h"

// ====== records_private ======

static int records_private(const KERNEL_MADT_CPU *records, uint32_t count, uint32_t bsp_id,
    uint32_t bsp_uid, X86_CPU_IDENTITIES *draft) {
    uint32_t matches = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const KERNEL_MADT_CPU *cpu = &records[index];
        uint32_t state = xi_record_state(cpu->Uid, cpu->Id, cpu->Flags, cpu->Kind);
        if (!state) return 0;
        if (state == 1) { ++draft->Ignored; continue; }
        for (uint32_t previous = 0; previous < index; ++previous) {
            const KERNEL_MADT_CPU *other = &records[previous];
            if (other->Flags && !xi_distinct(cpu->Uid, cpu->Id, other->Uid, other->Id)) return 0;
        }
        if (state == 2) { ++draft->Offline; continue; }
        ++draft->Count;
        if (cpu->Id == bsp_id && cpu->Uid == bsp_uid) {
            ++matches; draft->Cpus[0] = (X86_CPU_IDENTITY){0, cpu->Id, cpu->Uid, cpu->Kind};
        }
    }
    return matches == 1;
}

// ====== x86_cpu_identity_plan ======

int x86_cpu_identity_plan(const KERNEL_MADT_CPU *records, uint32_t count,
    uint32_t bsp_id, uint32_t bsp_uid, X86_CPU_IDENTITIES *output) {
    if (!count || count > X86_PERCPU_CAPACITY || !ks_span(records, (size_t)count * sizeof(*records)) ||
        !ks_span(output, sizeof(*output)) ||
        ks_overlap(records, (size_t)count * sizeof(*records), output, sizeof(*output))) return 0;
    X86_CPU_IDENTITIES draft = {0};
    if (!records_private(records, count, bsp_id, bsp_uid, &draft)) return 0;
    uint32_t next = 1;
    for (uint32_t index = 0; index < count; ++index) {
        const KERNEL_MADT_CPU *cpu = &records[index];
        if (cpu->Flags != 1 || (cpu->Id == bsp_id && cpu->Uid == bsp_uid)) continue;
        draft.Cpus[next] = (X86_CPU_IDENTITY){next, cpu->Id, cpu->Uid, cpu->Kind};
        ++next;
    }
    if (next != draft.Count) return 0;
    draft.Self = (uintptr_t)output; draft.Ready = 1;
    for (uint32_t byte = 0; byte < sizeof(draft); ++byte)
        ((unsigned char *)output)[byte] = ((const unsigned char *)&draft)[byte];
    return 1;
}
