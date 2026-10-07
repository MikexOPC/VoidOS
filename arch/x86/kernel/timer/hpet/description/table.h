#ifndef X86_HPET_DESCRIPTION_TABLE_H
#define X86_HPET_DESCRIPTION_TABLE_H

#include <stddef.h>
#include <stdint.h>

#define X86_HPET_TABLE_BYTES 56u
#define X86_HPET_REGISTER_BYTES 1024u

typedef struct {
    uint64_t  Physical;
    uint32_t  BlockId;
    uint16_t  MinimumTick;
    uint8_t   Number;
    uint8_t   PageAttributes;
} X86_HPET_DESCRIPTION;

_Static_assert(sizeof(X86_HPET_DESCRIPTION) == 16, "HPET description ABI");
_Static_assert(offsetof(X86_HPET_DESCRIPTION, PageAttributes) == 15, "HPET attributes ABI");

/* Caller owns readable bytes and writable aligned output. No physical address
 * is dereferenced here. Failure preserves output; overlapping spans fail. */
int x86_hpet_description_decode(const unsigned char *bytes, size_t length,
    uint32_t physical_bits, X86_HPET_DESCRIPTION *output);

/* Pure numeric Rust policy; no pointers, allocation or device access. */
uint32_t x86_hpet_description_fields(uint32_t block_id, uint32_t gas,
    uint64_t physical, uint32_t physical_bits, uint32_t attributes);

#endif
