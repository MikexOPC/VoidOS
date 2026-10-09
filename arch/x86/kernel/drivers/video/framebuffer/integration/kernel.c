#include "kernel.h"
#include "kernel/diagnostics/log.h"
static X86_FB_MAPPING mapping;

// ====== x86_fb_kernel_start ======

int x86_fb_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory)
{
    if (!kernel_section("mapping framebuffer kernel") ||
        !x86_fb_bind(context, window, lapic, inventory, &mapping)) return 0;
    if (!context->Info.Framebuffer.Base) return kernel_log(1, "KERNEL_FB_MAPPING_ABSENT");
#ifdef KERNEL_TEST_FRAMEBUFFER_MAPPING
    {
        BOOT_U64 budget = mapping.Budget;
        if (!x86_fb_unbind(&mapping) || window->Pool->FreePages != budget ||
            !x86_paging_inventory_verify_lapic(context, window->Pool, lapic, inventory) ||
            !kernel_log(0, "KERNEL_FB_ROLLBACK_OK") ||
            !x86_fb_bind(context, window, lapic, inventory, &mapping)) return 0;
    }
#endif
    if (!x86_paging_inventory_verify_video(context, window->Pool, lapic, &mapping.Authority,
        x86_fb_authority, inventory)) return 0;
    return kernel_text("KERNEL_FB_ROOT: ") && kernel_number(mapping.Authority.Root) &&
        kernel_text("\nKERNEL_FB_OWNER: ") && kernel_number((BOOT_U64)&mapping) &&
        kernel_text("\nKERNEL_FB_VIRTUAL: ") && kernel_number(X86_FB_VIRTUAL) &&
        kernel_text("\nKERNEL_FB_TABLE0: ") && kernel_number(mapping.Authority.Tables[0].Base) &&
        kernel_text("\nKERNEL_FB_TABLE_COUNT: ") && kernel_number(mapping.Allocated) &&
        kernel_text("\nKERNEL_FB_CACHE: ") && kernel_number(mapping.Authority.Region.Cache) &&
        kernel_text("\nKERNEL_FB_PAT: ") && kernel_number(mapping.Authority.Region.Pat) &&
        kernel_text("\nKERNEL_FB_BUDGET_BEFORE: ") && kernel_number(mapping.Budget) &&
        kernel_text("\nKERNEL_FB_BUDGET_LIVE: ") && kernel_number(window->Pool->FreePages) &&
        kernel_text("\nKERNEL_FB_MAPPING_INVENTORY: ") && kernel_number((BOOT_U64)inventory) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_FB_MAPPING_LIVE");
}
