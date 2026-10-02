#ifndef OS_KERNEL_FILE_BUFFER_H
#define OS_KERNEL_FILE_BUFFER_H
#include "kernel_file.h"

EFI_STATUS boot_adopt_kernel_file_buffer(EFI_SYSTEM_TABLE *table, EFI_FILE_PREFIX *root,
                                        EFI_FILE_PREFIX *file, const KERNEL_FILE_REPORT *report,
                                        BOOT_KERNEL_IMAGE *kernel, void *buffer, UINTN bytes);
#endif
