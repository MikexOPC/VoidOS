#include "../table.h"

// ====== write_le ======

static void write_le(unsigned char *bytes, uint64_t value, unsigned count)
{
    unsigned index;
    for (index = 0; index < count; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8u));
    }
}

// ====== checksum ======

static void checksum(unsigned char *bytes)
{
    unsigned sum = 0;
    unsigned index;
    bytes[9] = 0;
    for (index = 0; index < X86_HPET_TABLE_BYTES; ++index) {
        sum += bytes[index];
    }
    bytes[9] = (unsigned char)(0u - sum);
}

// ====== fixture ======

static void fixture(unsigned char *bytes)
{
    unsigned index;
    for (index = 0; index < X86_HPET_TABLE_BYTES; ++index) {
        bytes[index] = 0;
    }
    bytes[0] = 'H'; bytes[1] = 'P'; bytes[2] = 'E'; bytes[3] = 'T';
    write_le(bytes + 4, X86_HPET_TABLE_BYTES, 4);
    bytes[8] = 1;
    write_le(bytes + 36, 0x8086a201, 4);
    write_le(bytes + 44, 0xfed00000, 8);
    checksum(bytes);
}

// ====== rejected_preserves_output ======

static int rejected_preserves_output(const unsigned char *bytes, size_t length,
    uint32_t bits)
{
    X86_HPET_DESCRIPTION output;
    unsigned char *raw = (unsigned char *)&output;
    unsigned index;
    for (index = 0; index < sizeof(output); ++index) {
        raw[index] = 0xa5;
    }
    if (x86_hpet_description_decode(bytes, length, bits, &output)) {
        return 0;
    }
    for (index = 0; index < sizeof(output); ++index) {
        if (raw[index] != 0xa5) {
            return 0;
        }
    }
    return 1;
}

// ====== run_hpet_description_tests ======

int run_hpet_description_tests(void)
{
    unsigned char storage[X86_HPET_TABLE_BYTES + 2];
    unsigned char *bytes = storage + 1;
    X86_HPET_DESCRIPTION output;
    _Alignas(X86_HPET_DESCRIPTION) unsigned char overlap[64];
    unsigned index;
    static const unsigned offsets[] = {0, 4, 8, 36, 37, 40, 41, 42, 43, 44, 55};
    static const unsigned char values[] = {'X', 55, 2, 0, 0xe2, 1, 32, 1, 1, 1, 3};
    storage[0] = 0x5a; storage[57] = 0x5b;
    fixture(bytes);
    if (!x86_hpet_description_decode(bytes, 56, 48, &output) ||
        output.Physical != 0xfed00000 || output.BlockId != 0x8086a201 ||
        output.MinimumTick != 0 || output.Number != 0 || output.PageAttributes != 0) {
        return 1;
    }
    bytes[41] = 64; bytes[52] = 7; bytes[55] = 0xf2;
    write_le(bytes + 53, 65535, 2); checksum(bytes);
    if (!x86_hpet_description_decode(bytes, 56, 48, &output) ||
        output.Number != 7 || output.MinimumTick != 65535 || output.PageAttributes != 0xf2) {
        return 2;
    }
    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        fixture(bytes); bytes[offsets[index]] = values[index]; checksum(bytes);
        if (!rejected_preserves_output(bytes, 56, 48)) {
            return (int)(10 + index);
        }
    }
    fixture(bytes); bytes[10] ^= 1;
    if (!rejected_preserves_output(bytes, 56, 48)) { return 30; }
    fixture(bytes);
    for (index = 0; index < 56; ++index) {
        if (!rejected_preserves_output(bytes, index, 48)) { return 31; }
    }
    if (!rejected_preserves_output(bytes, 57, 48) ||
        !rejected_preserves_output(bytes, SIZE_MAX, 48) ||
        !rejected_preserves_output(NULL, 56, 48) ||
        !rejected_preserves_output(bytes, 56, 53) ||
        x86_hpet_description_decode(bytes, 56, 48, NULL)) { return 32; }
    write_le(bytes + 44, (UINT64_C(1) << 48) - 1024, 8); checksum(bytes);
    if (!x86_hpet_description_decode(bytes, 56, 48, &output)) { return 33; }
    write_le(bytes + 44, (UINT64_C(1) << 48) - 1016, 8); checksum(bytes);
    if (!rejected_preserves_output(bytes, 56, 48)) { return 34; }
    write_le(bytes + 44, UINT64_MAX - 7, 8); checksum(bytes);
    if (!rejected_preserves_output(bytes, 56, 48)) { return 35; }
    fixture(overlap);
    if (x86_hpet_description_decode(overlap, 56, 48,
        (X86_HPET_DESCRIPTION *)(void *)(overlap + 8))) { return 36; }
    fixture(bytes);
    if (x86_hpet_description_decode(bytes, 56, 48,
        (X86_HPET_DESCRIPTION *)(void *)(overlap + 1))) { return 37; }
    if (storage[0] != 0x5a || storage[57] != 0x5b) { return 38; }
    return 0;
}
