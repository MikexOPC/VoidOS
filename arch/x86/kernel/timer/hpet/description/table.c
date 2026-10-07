#include "table.h"

/* Intel HPET 1.0a, section 3.2.4, Table 3: signature 0, length 4,
 * revision 8, checksum 9, block ID 36, GAS 40, number 52,
 * periodic minimum 53, page/OEM attributes 55. Decode bytes instead of
 * casting firmware data to packed structs, including unaligned tables.
 * Threat: corrupt firmware metadata must not cause out-of-bounds reads or
 * publish an invalid address. Fixed extent and checksum precede decoding;
 * numeric policy precedes publication. Caller must supply owned memory:
 * integer pointer checks do not prove mapping, lifetime or accessibility. */

// ====== read_le ======

static uint64_t read_le(const unsigned char *bytes, unsigned count)
{
    uint64_t value = 0;
    unsigned index;
    for (index = 0; index < count; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

// ====== spans_valid ======

static int spans_valid(const unsigned char *bytes, size_t length,
    const X86_HPET_DESCRIPTION *output)
{
    uintptr_t input = (uintptr_t)bytes;
    uintptr_t target = (uintptr_t)output;
    if (!bytes || !output || length != X86_HPET_TABLE_BYTES ||
        target % _Alignof(X86_HPET_DESCRIPTION) != 0 ||
        input > UINTPTR_MAX - length ||
        target > UINTPTR_MAX - sizeof(*output)) {
        return 0;
    }
    return input + length <= target || target + sizeof(*output) <= input;
}

// ====== x86_hpet_description_decode ======

int x86_hpet_description_decode(const unsigned char *bytes, size_t length,
    uint32_t physical_bits, X86_HPET_DESCRIPTION *output)
{
    X86_HPET_DESCRIPTION draft;
    unsigned index;
    unsigned checksum = 0;
    uint32_t gas;
    if (!spans_valid(bytes, length, output)) {
        return 0;
    }
    if (bytes[0] != 'H' || bytes[1] != 'P' || bytes[2] != 'E' ||
        bytes[3] != 'T' || read_le(bytes + 4, 4) != length || bytes[8] != 1) {
        return 0;
    }
    for (index = 0; index < X86_HPET_TABLE_BYTES; ++index) {
        checksum += bytes[index];
    }
    if ((checksum & 255u) != 0) {
        return 0;
    }
    draft.BlockId = (uint32_t)read_le(bytes + 36, 4);
    gas = (uint32_t)read_le(bytes + 40, 4);
    draft.Physical = read_le(bytes + 44, 8);
    draft.Number = bytes[52];
    draft.MinimumTick = (uint16_t)read_le(bytes + 53, 2);
    draft.PageAttributes = bytes[55];
    if (!x86_hpet_description_fields(draft.BlockId, gas, draft.Physical,
        physical_bits, draft.PageAttributes)) {
        return 0;
    }
    *output = draft;
    return 1;
}
