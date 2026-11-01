#include "group.h"
#include "../internal.h"
static XAG_GROUP descriptor;
_Alignas(8) static uint64_t publication;

// ====== apart_private ======

static int apart_private(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    return a <= b ? b - a >= an : a - b >= bn;
}

// ====== regions_private ======

static int regions_private(const XAG_CPU *a, const XAG_CPU *b) {
    uint64_t first[3] = {a->Local, a->Wait, (uint64_t)a->Expected.Clock.Context};
    uint64_t second[3] = {b->Local, b->Wait, (uint64_t)b->Expected.Clock.Context};
    const uint64_t sizes[3] = {448, 32, 24};
    for (uint32_t i = 0; i < 3; ++i) {
        for (uint32_t j = 0; j < 3; ++j)
            if (!apart_private(first[i], sizes[i], second[j], sizes[j])) return 0;
        if (!apart_private((uint64_t)a->Expected.Clock.Read, 1, second[i], sizes[i]) ||
            !apart_private((uint64_t)b->Expected.Clock.Read, 1, first[i], sizes[i])) return 0;
    }
    return 1;
}

// ====== input_private ======

static int input_private(const XAG_GROUP *input) {
    if (!input || ((uintptr_t)input & 7) || !ks_span(input, sizeof(*input)) ||
        ks_overlap(input, sizeof(*input), &descriptor, sizeof(descriptor)) ||
        !xag_header_valid(input->Header)) return 0;
    for (uint32_t index = 0; index < input->Header.Count; ++index) {
        const XAG_CPU *cpu = &input->Cpus[index];
        if (cpu->Index != index || !xag_cpu_valid(*cpu, input->Header.PhysicalBits)) return 0;
        uint64_t regions[4] = {cpu->Local, cpu->Wait, (uint64_t)cpu->Expected.Clock.Context,
            (uint64_t)cpu->Expected.Clock.Read};
        const uint64_t sizes[4] = {448, 32, 24, 1};
        for (uint32_t region = 0; region < 4; ++region)
            if (!apart_private(regions[region], sizes[region], (uint64_t)&descriptor, sizeof(descriptor)) ||
                !apart_private(regions[region], sizes[region], (uint64_t)input, sizeof(*input)) ||
                !apart_private(regions[region], sizes[region], (uint64_t)&publication, sizeof(publication))) return 0;
        for (uint32_t previous = 0; previous < index; ++previous)
            if (cpu->ApicId == input->Cpus[previous].ApicId || !regions_private(cpu, &input->Cpus[previous])) return 0;
    }
    for (uint32_t index = input->Header.Count; index < 64; ++index)
        for (uint32_t byte = 0; byte < sizeof(XAG_CPU); ++byte)
            if (((const unsigned char *)&input->Cpus[index])[byte]) return 0;
    return 1;
}

// ====== xag_publish ======

int xag_publish(const XAG_GROUP *input) {
    if (ks_irq_state() || !ks_boot_cpu() || ks_atomic_load_acquire(&publication) ||
        !input_private(input) || !ks_atomic_compare(&publication, 0, 1)) return 0;
    for (uint32_t byte = 0; byte < sizeof(descriptor); ++byte)
        ((volatile unsigned char *)&descriptor)[byte] = ((const unsigned char *)input)[byte];
    descriptor.Header.Self = (uint64_t)&descriptor;
    ks_atomic_store_release(&publication, 2);
    return 1;
}

// ====== xag_group ======

const XAG_GROUP *xag_group(void) {
    if (ks_atomic_load_acquire(&publication) != 2) return NULL;
    return &descriptor;
}

// ====== xag_match ======

const XAG_CPU *xag_match(uint32_t index, uint32_t apic_id, uint64_t local) {
    const XAG_GROUP *group = xag_group();
    if (!group || index >= group->Header.Count) return NULL;
    const XAG_CPU *cpu = &group->Cpus[index];
    return cpu->ApicId == apic_id && cpu->Local == local ? cpu : NULL;
}
