#ifndef OS_KERNEL_IMAGE_H
#define OS_KERNEL_IMAGE_H
#include "handoff/segments.h"

// ====== proprietario delle pagine e descrizione valida mentre sono vive ====== //

typedef struct {
    ELF_STAGED_IMAGE Pages;
    BOOT_SEGMENTS Description;
} BOOT_KERNEL_IMAGE;

EFI_STATUS boot_release_kernel(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_IMAGE *kernel);
#endif
