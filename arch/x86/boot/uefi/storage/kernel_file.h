#ifndef OS_KERNEL_FILE_H
#define OS_KERNEL_FILE_H
#include "file_protocol.h"
#include "elf/validate.h"
#include "kernel_image.h"

// ====== report della verifica kernel ====== //

typedef struct {
    UINT64 FileBytes;
    ELF_VALIDATION_RESULT Validation;
    ELF_LOAD_PLAN Plan;
} KERNEL_FILE_REPORT;

EFI_STATUS boot_validate_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, KERNEL_FILE_REPORT *report);
EFI_STATUS boot_load_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, KERNEL_FILE_REPORT *report, BOOT_KERNEL_IMAGE *kernel);
#endif
