#ifndef OS_X86_LIB_PAGING_VALIDATION_H
#define OS_X86_LIB_PAGING_VALIDATION_H

#include <stdint.h>

#define X86_PAGING_NXE 1U
#define X86_PAGING_WP 2U
#define X86_PAGING_PCID 4U
#define X86_PAGING_VALID 0x80000000U
#define X86_PAGING_ROOT_MASK UINT64_C(0x000ffffffffff000)

#ifdef __cplusplus
extern "C" {
#endif

// ====== interfaccia C dei controlli numerici Rust senza allocazioni ====== //

uint32_t x86_validate_paging_profile(uint32_t maximum_leaf, uint32_t features, uint32_t address_sizes);
uint32_t x86_validate_paging_state(uint64_t cr0, uint64_t cr3, uint64_t cr4, uint64_t efer, uint32_t physical_bits);

#ifdef __cplusplus
}
#endif

#endif
