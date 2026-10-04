#include "arch/x86/kernel/timer/lapic/timer.h"

// ====== servizi non esercitati nei test di parsing calibrazione e handler ===== //

int x86_irq_ready(void) { return 1; }
int x86_lapic_authority(const X86_LAPIC_MAPPING *mapping) { return mapping != 0; }
BOOT_U32 x86_apic_lapic_version(BOOT_U32 value) { return value == 0x50014; }
X86_IRQ_REGISTRY *x86_irq_registry(void) { return 0; }
int x86_irq_registry_bind(X86_IRQ_REGISTRY *registry, BOOT_U32 vector,
    X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK quiet, X86_IRQ_TOKEN *token)
{
    (void)registry; (void)vector; (void)handle; (void)quiet; (void)token;
    return 0;
}
int x86_irq_registry_unbind(X86_IRQ_REGISTRY *registry, X86_IRQ_TOKEN *token)
{
    (void)registry; (void)token; return 0;
}
BOOT_U64 x86_irq_registry_deliveries(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token)
{
    (void)registry; (void)token; return 0;
}
int x86_irq_wait(volatile BOOT_U32 *count, BOOT_U32 goal, BOOT_U32 budget)
{
    (void)count; (void)goal; (void)budget; return 0;
}
int x86_page_window_read_acpi(X86_PAGE_WINDOW *window, const KERNEL_BOOT_CONTEXT *context,
    BOOT_U64 physical, BOOT_U64 bytes, unsigned char *output)
{
    (void)window; (void)context; (void)physical; (void)bytes; (void)output; return 0;
}
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *window) { return window != 0; }
int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_INVENTORY *inventory)
{
    (void)context; (void)pool; (void)mapping; (void)inventory; return 0;
}
int kernel_text(const char *text) { return text != 0; }
int kernel_section(const char *text) { return text != 0; }
int kernel_log(unsigned int level, const char *message) { (void)level; return message != 0; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
