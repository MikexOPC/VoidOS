#ifndef OS_TRANSITION_CAPSULE_H
#define OS_TRANSITION_CAPSULE_H
#include "uefi.h"

// ====== codice autonomo senza riferimenti esterni dopo la copia ====== //

#define BOOT_CAPSULE_BYTES 580U
#define BOOT_CAPSULE_VECTOR_OFFSET 64U
#define BOOT_CAPSULE_VECTOR_STRIDE 16U
#define BOOT_CAPSULE_STOP_OFFSET 576U
#define BOOT_CAPSULE_PAGE_OFFSET (9U * 4096U)

extern const UINT8 x86_boot_capsule_begin[];
extern const UINT8 x86_boot_capsule_end[];
UINT32 boot_capsule_expected_byte(UINT32 offset);
UINT32 boot_capsule_check_addresses(UINT64 code, UINT64 entry, UINT64 stack_top,
                                    UINT64 contract, UINT64 stack_bytes);
int boot_prepare_capsule(UINT8 *page, UINTN bytes);
int boot_validate_capsule(const UINT8 *page, UINTN bytes);
#endif
