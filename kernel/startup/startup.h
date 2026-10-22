#ifndef KERNEL_STARTUP_H
#define KERNEL_STARTUP_H
#include "../bootstrap/memory.h"
#include "../drivers/video/framebuffer/handoff.h"
#include "../drivers/video/framebuffer/mapping.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "../scheduler/kernel.h"
#include "../diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/mm/paging/inventory.h"
#include "../firmware/acpi/platform.h"
#include "../firmware/acpi/madt/platform.h"
#include "arch/x86/kernel/interrupts/apic/profile.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/timer/hpet/description/integration/kernel.h"

// Startup state owned by kernel/main.c; the steps borrow it, nothing here outlives the boot.
typedef struct {
    KERNEL_BOOTSTRAP_MEMORY  *Memory;
    X86_HPET_DESCRIPTION  *Hpet;
    KERNEL_OBJECT_HANDLE  *Paging, *Acpi, *Madt;
    X86_APIC_PROFILE  *ApicProfile;
    X86_APIC_CAPABILITIES  *ApicCapabilities;
} KERNEL_STARTUP;

// ====== startup_steps ======

// Order and conditions are documented in kernel/documentation/main.md; every step panics on failure.
void kernel_startup_early(KERNEL_BOOTSTRAP_MEMORY *memory, const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry);
void kernel_startup_platform(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT **context, X86_PAGING_INVENTORY **paging);
void kernel_startup_devices(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging);

// ====== profile_hooks ======

// The only place where diagnostic profiles change the sequence; empty in the production image.
void kernel_profile_after_retire(void);
void kernel_profile_after_acpi(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging);
int kernel_profile_devices(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging);
__declspec(noreturn) void kernel_profile_finish(const KERNEL_STARTUP *startup);
#endif
