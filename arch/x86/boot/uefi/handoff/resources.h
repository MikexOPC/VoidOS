#ifndef OS_BOOT_RESOURCES_H
#define OS_BOOT_RESOURCES_H
#include "storage/kernel_image.h"
#include "stack.h"
#include "memory/ownership.h"

EFI_STATUS boot_check_resources(const BOOT_MEMORY_MAP *map, const BOOT_KERNEL_IMAGE *kernel,
                                const BOOT_KERNEL_STACK *stack);
#endif
