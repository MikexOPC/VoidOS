#ifndef OS_LIB_MEMORY_BUFFER_CAPACITY_H
#define OS_LIB_MEMORY_BUFFER_CAPACITY_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== calcoli numerici Rust senza accesso ai buffer ====== //

uint64_t os_buffer_capacity(uint64_t needed, uint64_t entry_bytes, uint64_t extra_entries,
                            uint64_t alignment, uint64_t maximum);
uint32_t os_buffer_extent_is_valid(uint64_t base, uint64_t capacity, uint64_t maximum);

#ifdef __cplusplus
}
#endif

#endif
