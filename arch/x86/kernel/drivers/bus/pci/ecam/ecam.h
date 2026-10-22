#ifndef X86_PCI_ECAM_H
#define X86_PCI_ECAM_H
#include "kernel/firmware/acpi/root.h"
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#define X86_MCFG_BYTES 556U
typedef struct { BOOT_U64 Base; unsigned short Segment; unsigned char First, Last; BOOT_U32 Reserved; } X86_ECAM_RANGE;
typedef struct { BOOT_U64 Physical; BOOT_U32 Count, Bits; X86_ECAM_RANGE Ranges[32]; } X86_MCFG;
_Static_assert(sizeof(X86_ECAM_RANGE)==16 && sizeof(X86_MCFG)==528,"MCFG snapshot ABI");
typedef struct {
    BOOT_U64  Self, Budget, Root, Pat, Cache;
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
    const X86_LAPIC_MAPPING  *Lapic;
    const X86_PAGING_INVENTORY  *Inventory;
    X86_MCFG  Info;
    BOOT_U32  Ready, Busy;
} X86_ECAM;
BOOT_U64 x86_ecam_range_end(BOOT_U64 base,BOOT_U32 first,BOOT_U32 last,BOOT_U32 bits);
BOOT_U64 x86_ecam_page(BOOT_U64 base,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function);
BOOT_U32 x86_ecam_offset(BOOT_U32 offset);
int x86_mcfg_valid(const X86_MCFG *info);
int x86_mcfg_decode(const unsigned char *bytes,BOOT_U32 length,BOOT_U32 bits,X86_MCFG *output);
int x86_mcfg_find(const KERNEL_ACPI_DIRECTORY *directory,BOOT_U32 bits,KERNEL_ACPI_READ read,void *state,
    unsigned char *scratch,BOOT_U32 capacity,X86_MCFG *output);
int x86_ecam_open(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory,const X86_MCFG *info,X86_ECAM *owner);
int x86_ecam_read(X86_ECAM *owner,BOOT_U32 segment,BOOT_U32 bus,BOOT_U32 device,BOOT_U32 function,BOOT_U32 offset,BOOT_U32 *value);
int x86_ecam_close(X86_ECAM *owner);
int x86_ecam_current(const X86_ECAM *owner);
int x86_ecam_safe(const X86_ECAM *owner,BOOT_U64 base,BOOT_U64 bytes);
int x86_ecam_load(BOOT_U32 offset,BOOT_U32 *value);
#endif
