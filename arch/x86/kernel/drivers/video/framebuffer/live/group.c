#include "group.h"
#include "arch/x86/kernel/scheduler/mapping/native.h"
#include "../console/profile.h"
#include "../pixels/session.h"
#include "kernel/mm/physical/holdings.h"
#include "kernel/sync/lock.h"
#include "arch/x86/kernel/scheduler/integration/control.h"

typedef struct {
    const KERNEL_PHYSICAL_ALLOCATOR *Pool;
    const X86_PM_CLOCK *Clock;
    BOOT_U64 SavedIf, PagingGeneration, PoolGeneration;
    BOOT_U32 Start, MaxMasked;
    int Restart;
} X86_FB_AUDIT_SECTION;

// ====== budget_matches ======

static int budget_matches(const X86_FB_MAPPING *owner, const X86_PAGING_INVENTORY *inventory, BOOT_U64 pages)
{
    BOOT_U64 mapped = (BOOT_U64)owner->Allocated + pages + (inventory->Gpu.Ready == 1 ? X86_GPU_TABLES : 0);
    BOOT_U64 held = kernel_holdings_total();
    return held != UINT64_MAX && mapped <= owner->Budget && held <= owner->Budget - mapped &&
        kernel_physical_budget_matches(owner->Window->Pool, owner->Budget - mapped - held);
}

// ====== prepare ======

static int prepare(X86_FB_MAPPING *owner, X86_PAGING_INVENTORY *expected, BOOT_U64 *pages)
{
    const X86_PAGING_INVENTORY *original;
    if (x86_fb_target() && owner && !owner->Published) {
        *pages = 0;
        return x86_fb_target()->Valid(x86_fb_target());
    }
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        !owner->Context || !owner->Window || !owner->Window->Pool || !(original = owner->Inventory) ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES) return 0;
    *pages = x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root);
    if (original->Gpu.Ready > 1 || !*pages || *pages > (BOOT_U64)x86_scheduler_capacity() * 16 || *pages % 16 ||
        !budget_matches(owner, original, *pages) ||
        original->LeafPages > UINT64_MAX - *pages || original->RamPages > UINT64_MAX - *pages) return 0;
    *expected = *original;
    expected->LeafPages += *pages;
    expected->RamPages += *pages;
    return 1;
}

// ====== group_private ======

static __declspec(noinline) int group_private(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    X86_PAGING_INVENTORY expected, *original;
    BOOT_U64 pages;
    int result;
    if (!ticks || !scalars || !count || count > 8) return 0;

    uint64_t saved_if = kernel_preempt_section_enter();
    X86_PROFILE_BEGIN(prepare_start);
    int prepared = prepare(owner, &expected, &pages);
    X86_PROFILE_END(X86_PROFILE_PREPARE, prepare_start);

    if (!prepared) {
        kernel_preempt_section_leave(saved_if);
        return 0;
    }
    int redirect_only = (x86_fb_target() != 0 && !owner->Published);
    original = owner->Inventory;
    if (!redirect_only)
        owner->Inventory = &expected;
    result = pointer ? x86_fb_group_pointer(owner, column, row, scalars, count, foreground, background, ticks, caret, pointer) :
        x86_fb_group_caret(owner, column, row, scalars, count, foreground, background, ticks, caret);
    owner->Inventory = original;
    if (!redirect_only)
        result = result && pages == x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root) &&
            budget_matches(owner, original, pages);
    kernel_preempt_section_leave(saved_if);
    return result;
}

// ====== x86_fb_live_group_pointer ======

int x86_fb_live_group_pointer(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    if (!ticks ||
        !scalars ||
        !count ||
        count > 8 ||
        column > UINT32_MAX - count ||
        (caret != UINT32_MAX && caret >= count)
    )
        return 0;

    *ticks = 0;
    for (BOOT_U32 cell = 0; cell < count; ++cell) {
        BOOT_U64 elapsed = 0;
        BOOT_U32 cell_caret = caret == cell ? 0 : UINT32_MAX;
        if (!group_private(owner,column + cell,row,scalars + cell,1,
                foreground,background,&elapsed,cell_caret,pointer) ||
            elapsed > UINT64_MAX - *ticks
        )
            return 0;

        *ticks += elapsed;
    }
    return 1;
}

// ====== audit_sample ======

static void audit_sample(X86_FB_AUDIT_SECTION *section) {
    BOOT_U32 now = x86_pm_read((unsigned short)section->Clock->Port);
    BOOT_U32 spent = (now - section->Start) & section->Clock->Mask;
    if (spent > section->MaxMasked
    )
        section->MaxMasked = spent;
}

// ====== audit_checkpoint ======

static int audit_checkpoint(void *opaque)
{
    X86_FB_AUDIT_SECTION *section = opaque;
    BOOT_U64 generation;
    audit_sample(section);
    kernel_preempt_section_leave(section->SavedIf);
    x86_scheduler_yield();
    section->SavedIf = kernel_preempt_section_enter();
    section->Start = x86_pm_read((unsigned short)section->Clock->Port);
    if (!kernel_physical_generation_read(section->Pool, &generation)) return 0;
    section->Restart = x86_paging_generation() != section->PagingGeneration ||
        generation != section->PoolGeneration;
    return !section->Restart;
}

// ====== audit_private ======

static __declspec(noinline) int audit_private(X86_FB_MAPPING *owner, X86_PAGING_INVENTORY *expected,
    const X86_PM_CLOCK *clock, BOOT_U32 *max_masked)
{
    X86_FB_AUDIT_SECTION section = {0};
    BOOT_U64 pages;
    int result = X86_FB_AUDIT_FAILED;

    section.Clock = clock;
    section.SavedIf = kernel_preempt_section_enter();
    section.Start = x86_pm_read((unsigned short)clock->Port);
    if (x86_fb_target() && owner && !owner->Published) {
        *max_masked = 0;
        return x86_fb_target()->Valid(x86_fb_target()) ? X86_FB_AUDIT_OK : X86_FB_AUDIT_FAILED;
    }
    if (prepare(owner, expected, &pages)) {
        section.Pool = owner->Window->Pool;
        section.PagingGeneration = x86_paging_generation();
        if (kernel_physical_generation_read(section.Pool, &section.PoolGeneration)) {
            int verified = x86_paging_inventory_verify_video_sliced(owner->Context, section.Pool, owner->Lapic,
                &owner->Authority, x86_fb_authority, expected, audit_checkpoint, &section);
            if (section.Restart) result = X86_FB_AUDIT_RETRY;
            else if (verified && pages == x86_scheduler_live_stack_pages_for(section.Pool, owner->Authority.Root) &&
                budget_matches(owner, owner->Inventory, pages)) result = X86_FB_AUDIT_OK;
        }
    }
    audit_sample(&section);
    kernel_preempt_section_leave(section.SavedIf);
    *max_masked = section.MaxMasked;
    return result;
}

// ====== x86_fb_live_audit ======

int x86_fb_live_audit(X86_FB_MAPPING *owner, const X86_PM_CLOCK *clock, BOOT_U32 *max_masked)
{
    X86_PAGING_INVENTORY expected;
    return audit_private(owner, &expected, clock, max_masked);
}

// ====== x86_fb_live_group_caret ======

int x86_fb_live_group_caret(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret)
{
    return x86_fb_live_group_pointer(owner, column, row, scalars, count, foreground, background, ticks, caret, NULL);
}

// ====== x86_fb_live_group ======

int x86_fb_live_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    return x86_fb_live_group_caret(owner, column, row, scalars, count, foreground, background, ticks, ~0U);
}
