#ifndef X86_DMAR_FIND_H
#define X86_DMAR_FIND_H
#include "table.h"
#include "kernel/firmware/acpi/root.h"
#define X86_DMAR_ABSENT 1
#define X86_DMAR_FOUND 2
int x86_dmar_find(const KERNEL_ACPI_DIRECTORY *directory,uint32_t bits,KERNEL_ACPI_READ read,void *context,
    unsigned char *scratch,size_t capacity,X86_DMAR_INFO *output);
#endif
