#ifndef OS_LIB_MEMORY_ADDRESS_RANGES_H
#define OS_LIB_MEMORY_ADDRESS_RANGES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== confronto numerico di intervalli senza accesso alla memoria ====== //

int os_address_ranges_overlap(uint64_t first, uint64_t first_bytes, uint64_t second, uint64_t second_bytes);

#ifdef __cplusplus
}
#endif

#endif
