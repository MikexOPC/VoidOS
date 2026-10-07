#include "scan.h"

// ====== arguments_valid ======

static int arguments_valid(const uint64_t *entries, uint32_t count,
    uint32_t bits, X86_HPET_READ read, const X86_HPET_DESCRIPTION *output)
{
    uintptr_t start = (uintptr_t)entries;
    uintptr_t end;
    uintptr_t target = (uintptr_t)output;
    uint32_t index;
    if (!read || !output || count > X86_HPET_DIRECTORY_LIMIT ||
        bits < 32 || bits > 52 || target % _Alignof(X86_HPET_DESCRIPTION) ||
        target > UINTPTR_MAX - sizeof(*output)) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }
    if (!entries || start % _Alignof(uint64_t) ||
        start > UINTPTR_MAX - (size_t)count * sizeof(*entries)) {
        return 0;
    }
    end = start + (size_t)count * sizeof(*entries);
    if (target < end && start < target + sizeof(*output)) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        if (!x86_hpet_table_span(entries[index], 4, bits)) {
            return 0;
        }
    }
    return 1;
}

// ====== x86_hpet_description_find ======

/* Intel HPET 1.0a sections 3.2.4 and 3.2.6: discover firmware-assigned
 * address from HPET before querying hardware. Threat: forged addresses,
 * unreadable tables and duplicate descriptions must not become an adopted
 * MMIO address. Numeric checks do not replace the reader's memory policy.
 * The fixed scan is O(count), at most 256 signature reads and one table copy.
 * No successful result is published before later entries are checked. */
int x86_hpet_description_find(const uint64_t *entries, uint32_t count,
    uint32_t physical_bits, X86_HPET_READ read, void *context,
    X86_HPET_DESCRIPTION *output)
{
    X86_HPET_DESCRIPTION draft = {0, 0, 0, 0, 0};
    unsigned char signature[4];
    unsigned char table[X86_HPET_TABLE_BYTES];
    uint32_t index;
    int found = 0;
    if (!arguments_valid(entries, count, physical_bits, read, output)) {
        return X86_HPET_INVALID;
    }
    for (index = 0; index < count; ++index) {
        if (read(context, entries[index], 4, signature) != 1) {
            return X86_HPET_READ_FAILED;
        }
        if (signature[0] != 'H' || signature[1] != 'P' ||
            signature[2] != 'E' || signature[3] != 'T') {
            continue;
        }
        if (found) {
            return X86_HPET_AMBIGUOUS;
        }
        if (!x86_hpet_table_span(entries[index], X86_HPET_TABLE_BYTES, physical_bits)) {
            return X86_HPET_INVALID;
        }
        if (read(context, entries[index], X86_HPET_TABLE_BYTES, table) != 1) {
            return X86_HPET_READ_FAILED;
        }
        if (!x86_hpet_description_decode(table, sizeof(table), physical_bits, &draft)) {
            return X86_HPET_INVALID;
        }
        found = 1;
    }
    if (!found) {
        return X86_HPET_ABSENT;
    }
    *output = draft;
    return X86_HPET_FOUND;
}
