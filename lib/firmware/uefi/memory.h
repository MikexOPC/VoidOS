#ifndef OS_UEFI_MEMORY_H
#define OS_UEFI_MEMORY_H

// UEFI 2.10 7.2 EFI_MEMORY_DESCRIPTOR.Attribute: UC 0x1, WC 0x2, WT 0x4, WB 0x8, UCE 0x10 are
// cacheability capabilities; XP 0x4000 marks the range non-executable for the firmware. The
// kernel enforces NX with its own paging, so XP does not exclude ConventionalMemory (B8).
// Same value as KERNEL_CONVENTIONAL_ATTRIBUTES in kernel/mm/rust/regions/validation.rs.
#define OS_UEFI_CACHEABILITY_ATTRIBUTES  0x1fULL
#define OS_UEFI_EXECUTE_PROTECTION       0x4000ULL
#define OS_UEFI_CONVENTIONAL_ATTRIBUTES  (OS_UEFI_CACHEABILITY_ATTRIBUTES | OS_UEFI_EXECUTE_PROTECTION)

#endif
