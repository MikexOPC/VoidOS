#ifndef X86_HPET_DESCRIPTION_SCAN_H
#define X86_HPET_DESCRIPTION_SCAN_H

#include "table.h"

#define X86_HPET_DIRECTORY_LIMIT 256u
#define X86_HPET_FOUND 1
#define X86_HPET_ABSENT 0
#define X86_HPET_INVALID (-1)
#define X86_HPET_READ_FAILED (-2)
#define X86_HPET_AMBIGUOUS (-3)

/* Same read shape as KERNEL_ACPI_READ; success means every requested byte
 * was copied. The reader enforces actual mapping and ACPI memory ownership.
 * It must not mutate entries or output, recurse or retain local buffers. */
typedef int (*X86_HPET_READ)(void *context, uint64_t physical,
    uint32_t bytes, unsigned char *output);

/* Caller supplies immutable entries from a validated ACPI directory and
 * owned output disjoint from entries/context. No allocation or MMIO access.
 * Empty directory permits NULL entries. Every non-FOUND result preserves
 * output. Multiple HPET signatures, including repeated addresses, fail. */
int x86_hpet_description_find(const uint64_t *entries, uint32_t count,
    uint32_t physical_bits, X86_HPET_READ read, void *context,
    X86_HPET_DESCRIPTION *output);

uint32_t x86_hpet_table_span(uint64_t physical, uint32_t bytes,
    uint32_t physical_bits);

#endif
