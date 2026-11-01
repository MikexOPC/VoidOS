#include "panic.h"
static XP_PANIC_RECORD *owners[64];

// ====== span_private ======

static int span_private(const void *object, size_t size, uintptr_t alignment) {
    return object && !((uintptr_t)object & (alignment - 1)) && (uintptr_t)object <= UINTPTR_MAX - size;
}

// ====== separate_private ======

static int separate_private(const void *a, size_t a_size, const void *b, size_t b_size) {
    return (uintptr_t)a + a_size <= (uintptr_t)b || (uintptr_t)b + b_size <= (uintptr_t)a;
}

// ====== owner_private ======

static int owner_private(const XP_PANIC_RECORD *record) {
    if (!span_private(record, sizeof(*record), 64)) return 0;
    uint32_t index = 0;
    for (; index < 64; ++index) if (owners[index] == record) break;
    if (index == 64 || record->Self != (uintptr_t)record || record->Version != 1 || record->Index != index) return 0;
    for (uint32_t word = 0; word < 33; ++word) if (record->Reserved[word]) return 0;
    return 1;
}

// ====== xp_panic_record_initialize ======

int xp_panic_record_initialize(XP_PANIC_RECORD *record, uint32_t index) {
    if (xp_panic_value(XP_PANIC_READY) || !span_private(record, sizeof(*record), 64) || index >= 64 || owners[index]) return 0;
    if (!separate_private(record, sizeof(*record), owners, sizeof(owners)) ||
        !separate_private(record, sizeof(*record), (const void *)(uintptr_t)xp_panic_state_address(), XP_PANIC_STATE_BYTES)) return 0;
    for (uint32_t slot = 0; slot < 64; ++slot)
        if (owners[slot] && !separate_private(record, sizeof(*record), owners[slot], sizeof(*record))) return 0;
    for (uint32_t byte = 0; byte < sizeof(*record); ++byte)
        if (((const unsigned char *)record)[byte]) return 0;
    record->Self = (uintptr_t)record; record->Version = 1; record->Index = index; owners[index] = record;
    return 1;
}

// ====== xp_panic_record_capture ======

int xp_panic_record_capture(XP_PANIC_RECORD *record, const XP_PANIC_INPUT *input) {
    if (xp_panic_value(XP_PANIC_READY) != 2 || !owner_private(record) || !span_private(input, sizeof(*input), 8) ||
        !separate_private(record, sizeof(*record), input, sizeof(*input)) ||
        !separate_private(input, sizeof(*input), (const void *)(uintptr_t)xp_panic_state_address(), XP_PANIC_STATE_BYTES) ||
        !xp_panic_input_valid(input->Kind, input->Vector, input->Error, input->Cs, input->Flags, input->Ss) ||
        !xp_panic_record_claim((uint32_t)record->Index)) return 0;
    for (uint32_t byte = 0; byte < sizeof(*input); ++byte)
        ((volatile unsigned char *)&record->Data)[byte] = ((const volatile unsigned char *)input)[byte];
    return (int)xp_panic_record_publish((uint32_t)record->Index);
}

// ====== xp_panic_record_snapshot ======

int xp_panic_record_snapshot(const XP_PANIC_RECORD *record, XP_PANIC_INPUT *output) {
    if (xp_panic_value(XP_PANIC_READY) != 2 || !owner_private(record) || !span_private(output, sizeof(*output), 8) ||
        !separate_private(record, sizeof(*record), output, sizeof(*output)) ||
        xp_panic_record_state((uint32_t)record->Index) != XP_PANIC_COMPLETE) return 0;
    if (!separate_private(output, sizeof(*output), owners, sizeof(owners)) ||
        !separate_private(output, sizeof(*output), (const void *)(uintptr_t)xp_panic_state_address(), XP_PANIC_STATE_BYTES)) return 0;
    for (uint32_t slot = 0; slot < 64; ++slot)
        if (owners[slot] && !separate_private(output, sizeof(*output), owners[slot], sizeof(*record))) return 0;
    const XP_PANIC_INPUT *input = &record->Data;
    if (!xp_panic_input_valid(input->Kind, input->Vector, input->Error, input->Cs, input->Flags, input->Ss)) return 0;
    for (uint32_t byte = 0; byte < sizeof(*output); ++byte)
        ((volatile unsigned char *)output)[byte] = ((const volatile unsigned char *)input)[byte];
    return 1;
}

// ====== xp_panic_record_retire ======

int xp_panic_record_retire(XP_PANIC_RECORD *record) {
    if (xp_panic_value(XP_PANIC_READY) || !owner_private(record)) return 0;
    owners[record->Index] = NULL;
    for (uint32_t byte = 0; byte < sizeof(*record); ++byte) ((volatile unsigned char *)record)[byte] = 0;
    return 1;
}
