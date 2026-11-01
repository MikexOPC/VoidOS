#include "blob.h"

// ====== word_private ======

static uint32_t word_private(const unsigned char *bytes, uint32_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8);
}

// ====== span_private ======

static int span_private(const void *pointer) {
    return pointer && (uintptr_t)pointer <= UINTPTR_MAX - (XB_PAGE - 1);
}

// ====== overlap_private ======

static int overlap_private(const void *a, const void *b) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < XB_PAGE : x - y < XB_PAGE;
}

// ====== valid_private ======

static int valid_private(const unsigned char *code, size_t code_capacity, const unsigned char *data,
    size_t data_capacity, const unsigned char *t, size_t bytes, XS_ENTRY_RECORD record, uint32_t bits) {
    if (code_capacity < XB_PAGE || data_capacity < XB_PAGE || bytes != XB_PAGE ||
        !span_private(code) || !span_private(data) || !span_private(t) ||
        overlap_private(code, data) || overlap_private(code, t) || overlap_private(data, t) ||
        !xs_entry_valid(record, bits) || t[0] != 0xeb || t[1] != 30 ||
        t[2] != 'X' || t[3] != 'A' || t[4] != 'P' || t[5] != '1' ||
        word_private(t, 6) != 1 || word_private(t, 30)) return 0;
    for (uint32_t i = 10; i < 16; ++i) if (t[i]) return 0;
    if (!xs_blob_layout(word_private(t, 8), word_private(t, 16), word_private(t, 18), word_private(t, 20),
        word_private(t, 22), word_private(t, 24), word_private(t, 26), word_private(t, 28))) return 0;
    for (uint32_t i = word_private(t, 8); i < XB_PAGE; ++i) if (t[i]) return 0;
    const uint64_t gdt[4] = {0, 0x00cf9b000000ffffULL, 0x00cf93000000ffffULL, 0x00af9b000000ffffULL};
    for (uint32_t i = 0; i < 32; ++i)
        if (t[word_private(t, 20) + i] != (unsigned char)(gdt[i / 8] >> (8 * (i % 8)))) return 0;
    return 1;
}

// ====== code_private ======

static unsigned char code_private(const unsigned char *t, XS_ENTRY_RECORD record, uint32_t i) {
    if (i >= 10 && i < 12) return (unsigned char)((record.Data >> 4) >> (8 * (i - 10)));
    if (i >= 12 && i < 16) return (unsigned char)(record.Data >> (8 * (i - 12)));
    return t[i];
}

// ====== gate_private ======

// Intel SDM 253668-078US 6.11/6.14.1: interrupt gate P1/DPL0/type E, IST0.
static unsigned char gate_private(uint64_t target, uint32_t selector, uint32_t byte) {
    if (byte < 2) return (unsigned char)(target >> (8 * byte));
    if (byte < 4) return (unsigned char)(selector >> (8 * (byte - 2)));
    if (byte == 5) return 0x8e;
    if (byte >= 6 && byte < 12) return (unsigned char)(target >> (8 * (byte - 4)));
    return 0;
}

// ====== data_private ======

static unsigned char data_private(const unsigned char *t, const XS_ENTRY_RECORD *r, uint32_t i) {
    if (i < sizeof(*r)) return ((const unsigned char *)r)[i];
    if (i >= XB_IVT && i < XB_IDT32) {
        uint32_t index = (i - XB_IVT) / 4, byte = (i - XB_IVT) % 4;
        uint32_t value = byte < 2 ? word_private(t, index == 2 ? 22 : 24) : (uint32_t)(r->Code >> 4);
        return (unsigned char)(value >> (8 * (byte % 2)));
    }
    if (i >= XB_IDT32 && i < XB_IDT64)
        return gate_private(r->Code + word_private(t, 26), 8, (i - XB_IDT32) % 8);
    if (i >= XB_IDT64 && i < XB_METADATA_END)
        return gate_private(r->Code + word_private(t, 28), 24, (i - XB_IDT64) % 16);
    if (i >= 112 && i < 114) return (unsigned char)(31U >> (8 * (i - 112)));
    if (i >= 114 && i < 118) return (unsigned char)((r->Code + word_private(t, 20)) >> (8 * (i - 114)));
    if (i >= 120 && i < 124) return (unsigned char)((r->Code + word_private(t, 16)) >> (8 * (i - 120)));
    if (i >= 124 && i < 126) return (unsigned char)(8U >> (8 * (i - 124)));
    if (i >= 128 && i < 132) return (unsigned char)((r->Code + word_private(t, 18)) >> (8 * (i - 128)));
    if (i >= 132 && i < 134) return (unsigned char)(24U >> (8 * (i - 132)));
    if (i >= 136 && i < 138) return (unsigned char)(127U >> (8 * (i - 136)));
    if (i >= 138 && i < 142) return (unsigned char)((r->Data + XB_IVT) >> (8 * (i - 138)));
    if (i >= 144 && i < 146) return (unsigned char)(255U >> (8 * (i - 144)));
    if (i >= 146 && i < 150) return (unsigned char)((r->Data + XB_IDT32) >> (8 * (i - 146)));
    if (i >= 152 && i < 154) return (unsigned char)(511U >> (8 * (i - 152)));
    if (i >= 154 && i < 162) return (unsigned char)((r->Data + XB_IDT64) >> (8 * (i - 154)));
    return 0;
}

// ====== xb_build ======

int xb_build(unsigned char *code, size_t code_capacity, unsigned char *data, size_t data_capacity,
    const unsigned char *t, size_t bytes, XS_ENTRY_RECORD record, uint32_t bits) {
    if (!valid_private(code, code_capacity, data, data_capacity, t, bytes, record, bits)) return 0;
    for (uint32_t i = 0; i < XB_PAGE; ++i) {
        code[i] = code_private(t, record, i);
        data[i] = data_private(t, &record, i);
    }
    return 1;
}

// ====== xb_verify ======

int xb_verify(const unsigned char *code, size_t code_capacity, const unsigned char *data, size_t data_capacity,
    const unsigned char *t, size_t bytes, XS_ENTRY_RECORD record, uint32_t bits) {
    if (!valid_private(code, code_capacity, data, data_capacity, t, bytes, record, bits)) return 0;
    for (uint32_t i = 0; i < XB_PAGE; ++i)
        if (code[i] != code_private(t, record, i) || data[i] != data_private(t, &record, i)) return 0;
    return 1;
}
