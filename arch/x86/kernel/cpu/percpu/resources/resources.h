#ifndef OS_X86_AP_RESOURCES_H
#define OS_X86_AP_RESOURCES_H
#include "paging/authority.h"
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
#include "arch/x86/kernel/descriptors/segments.h"
typedef int (*X86_AP_CAPTURE)(const KERNEL_BOOT_CONTEXT *, const KERNEL_PHYSICAL_ALLOCATOR *,
    const X86_LAPIC_MAPPING *, X86_PAGING_INVENTORY *);
typedef struct {
    X86_AP_AUTH  Authority;
    BOOT_U64  Self, Budget;
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
    const X86_LAPIC_MAPPING  *Lapic;
    X86_PAGING_INVENTORY  *Inventory;
    X86_AP_CAPTURE  Capture;
    BOOT_U32  DataAllocated, DataPinned, TablesAllocated, TablesPinned, Published, Registered, Committed;
} X86_AP_RESOURCES;
int x86_ap_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_AP_CAPTURE capture, BOOT_U32 cpus, X86_AP_RESOURCES *owner);
int x86_ap_unbind(X86_AP_RESOURCES *owner);
int x86_ap_commit(X86_AP_RESOURCES *owner);
int x86_ap_resources_commit(void);
const X86_AP_RESOURCES *x86_ap_resources_preflight(void);
int x86_ap_resources_joint_seal(const X86_AP_RESOURCES *expected);
int x86_ap_resources_committed_valid(const X86_AP_RESOURCES *expected);
int x86_ap_resources_joint_ready(void);
int x86_ap_check(const X86_AP_AUTH *auth);
int x86_ap_construct(X86_AP_RESOURCES *owner);
int x86_ap_descriptor_prepare(X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu);
int x86_ap_descriptor_verify(const X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu);
int x86_ap_resources_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
#endif
