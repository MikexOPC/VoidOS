#ifndef OS_KERNEL_ACPI_ROOT_H
#define OS_KERNEL_ACPI_ROOT_H
#include "lib/firmware/acpi/rsdp.h"
#define KERNEL_ACPI_ROOT_ENTRIES 256U
typedef int (*KERNEL_ACPI_READ)(void *context, OS_ACPI_U64 physical, OS_ACPI_U32 bytes, unsigned char *output);
typedef struct {
    OS_ACPI_U64 Physical;
    OS_ACPI_U32 Length, Count, Width, Reserved;
    OS_ACPI_U64 Entries[KERNEL_ACPI_ROOT_ENTRIES];
} KERNEL_ACPI_DIRECTORY;
_Static_assert(sizeof(KERNEL_ACPI_DIRECTORY) == 2072, "Directory ACPI bounded");
OS_ACPI_U32 kernel_acpi_root_layout(OS_ACPI_U32 signature, OS_ACPI_U32 length,
                                    OS_ACPI_U32 revision, OS_ACPI_U32 width);
OS_ACPI_U32 kernel_acpi_physical_span(OS_ACPI_U64 physical, OS_ACPI_U32 bytes, OS_ACPI_U32 bits);
int kernel_acpi_root_capture(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits,
                             KERNEL_ACPI_READ read, void *context, KERNEL_ACPI_DIRECTORY *output);
int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *directory, OS_ACPI_U32 bits);
#endif
