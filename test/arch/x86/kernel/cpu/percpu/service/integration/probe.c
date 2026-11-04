#include "probe.h"
#include "arch/x86/mm/dma/arena.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/configs/boot_profile.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/stack/main.h"

// ====== number_private ======

static void number_private(const char *name, uint64_t value) {
    if (!kernel_text(name) || !kernel_number(value) || !kernel_text("\n")) xis_native_abort(0x4b344950494c4f47ULL);
}

#if KERNEL_TEST_AP_SERVICE == 7
// ====== timer_request ======

static void timer_request(uint32_t index, uint32_t operation)
{
    XIS_WAIT wait = {xis_native_now, NULL, xp_panic_native.Source.Mask, KERNEL_PM_TIMER_HZ, 1000000, 0};
    uint64_t generation = xis_publish_operation(index, 0, operation);
    if (!generation || generation == XIS_INVALID || !xis_native_send(index) ||
        xis_wait(index, generation, &wait) != XIS_WAIT_ACK || !xis_native_wait(index, 1, generation))
        xis_native_abort(0x4b34435452455100ULL);
}

// ====== timer_delay ======

static void timer_delay(uint32_t ticks)
{
    uint32_t start, now;
    if (!xis_native_now(NULL, &start)) xis_native_abort(0x4b344354434c4b00ULL);
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        if (!xis_native_now(NULL, &now)) break;
        if (((now - start) & xp_panic_native.Source.Mask) >= ticks) return;
        ks_pause();
    }
    xis_native_abort(0x4b34435457414954ULL);
}

// ====== timer_probe ======

__declspec(noreturn) static void timer_probe(void)
{
    uint32_t reload = x86_timer_diagnostic_reload();
    if (!reload) xis_native_abort(0x4b344354524c4400ULL);
    ks_atomic_store_release(&xis_timer_reload, reload);
    number_private("KERNEL_AP_TIMER_RELOAD: ", reload);
    for (uint32_t index = 1; index < xis_native.Count; ++index) timer_request(index, XIS_ARM_TIMER);
    for (uint32_t index = 1; index < xis_native.Count; ++index) timer_request(index, XIS_PING);
    if (!xis_native_shootdown()) xis_native_abort(0x4b344354464c5300ULL);
    timer_delay(KERNEL_PM_TIMER_HZ / 10);
    for (uint32_t index = 1; index < xis_native.Count; ++index) {
        if (ks_atomic_load_acquire(&xis_timer_ticks[index]) < 3) xis_native_abort(0x4b34435450524700ULL);
        timer_request(index, XIS_STOP_TIMER);
    }
    timer_delay(KERNEL_PM_TIMER_HZ / 25);
    uint64_t stopped[64];
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        stopped[index] = ks_atomic_load_acquire(&xis_timer_ticks[index]);
    timer_delay(KERNEL_PM_TIMER_HZ / 10);
    for (uint32_t index = 1; index < xis_native.Count; ++index) {
        if (ks_atomic_load_acquire(&xis_timer_ticks[index]) != stopped[index])
            xis_native_abort(0x4b34435453544f50ULL);
        number_private("KERNEL_AP_TIMER_TICKS: ", stopped[index]);
    }
    if (!kernel_log(0, "KERNEL_AP_TIMER_READY")) xis_native_abort(0x4b3443544c4f4700ULL);
    x86_kernel_stop(0x4b34495049504153ULL);
}
#endif

// ====== xis_service_probe ======

__declspec(noreturn) void xis_service_probe(const KERNEL_STARTUP *startup) {
#if defined(XIS_REMAP_TEST) || KERNEL_TEST_AP_SERVICE == 6
    X86_PAGE_WINDOW *window = &startup->Memory->Window;
#else
    (void)startup;
#endif
    if (ks_irq_state() || xis_native_index() != 0 || xis_native.Count < 2) xis_native_abort(0x4b34495049425350ULL);
    number_private("KERNEL_IPI_NATIVE: ", (uint64_t)&xis_native);
    number_private("KERNEL_IPI_IDTR: ", (uint64_t)&xis_native.Idtr);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 0, XIS_READY)) xis_native_abort(0x4b34495049524459ULL);
    // Boot-time shootdowns (scheduler task stacks) already used one generation per AP each.
    uint64_t base = xis_native_shootdowns();
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (xis_value(index, XIS_REQUEST) != base || xis_value(index, XIS_ACK) != base)
            xis_native_abort(0x4b34495049424153ULL);
    ks_atomic_store_release(&xis_probe_base, base);
    ks_atomic_store_release(&xis_probe_armed, 1);
    number_private("KERNEL_IPI_BASE_GENERATION: ", base);
#if KERNEL_TEST_AP_SERVICE == 7
    timer_probe();
#elif KERNEL_TEST_AP_SERVICE == 5
    if (xis_publish(1, 0) != base + 1 || !xis_native_send(1) || !xis_native_wait(1, 2, base + 1))
        xis_native_abort(0x4b34495049484e44ULL);
    xis_native_abort(0x4b344950494e4d35ULL);
#else
#ifdef XIS_REMAP_TEST
    KERNEL_PHYSICAL_LEASE old = {0}, replacement = {0};
    volatile unsigned char *view = (volatile unsigned char *)X86_KERNEL_PAGE_WINDOW;
    if (!x86_page_window_acquire(window, 1)) xis_native_abort(0x4b34544c42414c31ULL);
    *view = XIS_NEW_VALUE;
    if (!x86_page_window_park(window, &replacement) || !x86_page_window_acquire(window, 1) ||
        window->Lease.Base == replacement.Base) xis_native_abort(0x4b34544c42414c32ULL);
    *view = XIS_OLD_VALUE;
#endif
    for (uint32_t round = 1; round <= 100; ++round) {
#ifdef XIS_REMAP_TEST
        if (round == XIS_FLUSH_ROUND) {
            if (!x86_page_window_park(window, &old) || !x86_page_window_resume(window, &replacement) ||
                !xis_native_flush_context()) xis_native_abort(0x4b34544c4252454dULL);
            number_private("KERNEL_IPI_OLD_FRAME: ", old.Base);
            number_private("KERNEL_IPI_NEW_FRAME: ", window->Lease.Base);
        }
#endif
        for (uint32_t index = 1; index < xis_native.Count; ++index) {
            uint32_t operation = XIS_PING;
#ifdef XIS_REMAP_TEST
            if (round == XIS_FLUSH_ROUND) operation = XIS_FLUSH_CONTEXT;
#endif
            uint64_t generation = xis_publish_operation(index, 0, operation);
            if (generation != base + round || !xis_native_send(index)) xis_native_abort(0x4b3449504953454eULL);
            XIS_WAIT wait = {xis_native_now, NULL, xp_panic_native.Source.Mask, KERNEL_PM_TIMER_HZ, 1000000, 0};
            uint32_t result = xis_wait(index, generation, &wait);
            if (result != XIS_WAIT_ACK) {
                number_private("KERNEL_IPI_FAILED_PHASE: ", xis_value(index, XIS_PHASE));
                xis_native_abort(0x4b34495049544d4fULL);
            }
            if (!xis_native_wait(index, 1, generation)) xis_native_abort(0x4b34495049565246ULL);
        }
    }
    for (uint32_t index = 1; index < xis_native.Count; ++index) {
        if (xis_value(index, XIS_ACK) != base + 100 ||
            ks_atomic_load_acquire(&xis_native.Cpus[index].Delivered) != base + 100 ||
            ks_atomic_load_acquire(&xis_native.Cpus[index].Eois) != base + 100 ||
            !ks_atomic_load_acquire(&xis_native.Cpus[index].Spurious)) xis_native_abort(0x4b34495049454e44ULL);
        number_private("KERNEL_IPI_ACK: ", xis_value(index, XIS_ACK));
    }
#ifdef XIS_REMAP_TEST
    if (!kernel_physical_release_after_unmap(window->Pool, &old) || !x86_page_window_release(window))
        xis_native_abort(0x4b34544c4252454cULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, base + XIS_READ_ROUND +1)) xis_native_abort(0x4b34544c42444f4eULL);
#if KERNEL_TEST_AP_SERVICE == 1
    X86_REGION_MAPPER *regions = &startup->Memory->Regions;
    X86_REGION_HANDLE region = {0};
    if (!x86_region_acquire(regions, 2, 1, &region)) xis_native_abort(0x4b34544c42524731ULL);
    uint32_t slot = x86_region_slot(region.Virtual);
    if (!slot || slot > X86_REGION_SLOTS) xis_native_abort(0x4b34544c42524732ULL);
    number_private("KERNEL_IPI_REGION_FRAME: ", regions->Live[slot - 1].Base);
    if (!x86_region_release(regions, &region) || !x86_region_validate(regions))
        xis_native_abort(0x4b34544c42524733ULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, base + XIS_READ_ROUND +2)) xis_native_abort(0x4b34544c42524734ULL);
    X86_HEAP_BINDING temporary = {0};
    X86_PAGING_INVENTORY *inventory = (X86_PAGING_INVENTORY *)
        x86_object_arena_data(&startup->Memory->Arena, startup->Paging);
    const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)
        x86_object_arena_data(&startup->Memory->Arena, &startup->Memory->ContextObject);
    if (!x86_heap_bind(context, window, xp_panic_native.Lapic, inventory,
        x86_paging_inventory_capture_lapic, &temporary)) xis_native_abort(0x4b34544c42485031ULL);
    number_private("KERNEL_IPI_HEAP_DATA: ", temporary.Authority.Data.Base);
    number_private("KERNEL_IPI_HEAP_TABLES: ", temporary.Authority.Tables.Base);
    if (!x86_heap_unbind(&temporary) || x86_heap_owner() || x86_fb_root()[X86_HEAP_SLOT])
        xis_native_abort(0x4b34544c42485032ULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, base + XIS_READ_ROUND +3)) xis_native_abort(0x4b34544c42485033ULL);
    X86_DMA_ARENA dma = {0};
    if (!x86_dma_arena_bind(context, window, xp_panic_native.Lapic, inventory,
        x86_paging_inventory_capture_lapic, 4, &dma) || dma.Allocated != 3)
        xis_native_abort(0x4b34544c42444d31ULL);
    number_private("KERNEL_IPI_DMA_DATA: ", dma.Authority.Data.Base);
    for (uint32_t index = 0; index < dma.Allocated; ++index)
        number_private("KERNEL_IPI_DMA_TABLE: ", dma.Authority.Tables[index].Base);
    if (!x86_dma_arena_unbind(&dma) || x86_dma_owner() || x86_fb_root()[X86_DMA_SLOT])
        xis_native_abort(0x4b34544c42444d32ULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, base + XIS_READ_ROUND +4)) xis_native_abort(0x4b34544c42444d33ULL);
    X86_FB_MAPPING framebuffer = {0};
    if (!x86_fb_bind(context, window, xp_panic_native.Lapic, inventory, &framebuffer) ||
        !framebuffer.Published || !framebuffer.Allocated ||
        framebuffer.Allocated > X86_PAGING_VIDEO_TABLES) xis_native_abort(0x4b34544c42464231ULL);
    number_private("KERNEL_IPI_FB_TABLE_COUNT: ", framebuffer.Allocated);
    for (uint32_t index = 0; index < framebuffer.Allocated; ++index)
        number_private("KERNEL_IPI_FB_TABLE: ", framebuffer.Authority.Tables[index].Base);
    if (!x86_fb_unbind(&framebuffer) || x86_paging_video_owner() || x86_fb_root()[X86_FB_SLOT])
        xis_native_abort(0x4b34544c42464232ULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, base + XIS_READ_ROUND +5)) xis_native_abort(0x4b34544c42464233ULL);
#endif
    if (!kernel_log(0, "KERNEL_IPI_REMAP_VERIFIED")) xis_native_abort(0x4b34544c4252454cULL);
#endif
#if KERNEL_TEST_AP_SERVICE == 6
    // Ordinary release API with a withheld flush ACK: the coordinated panic must keep the lease.
    if (!x86_page_window_acquire(window, 1)) xis_native_abort(0x4b34544c42524c31ULL);
    number_private("KERNEL_IPI_RELEASE_FRAME: ", window->Lease.Base);
    *(volatile unsigned char *)X86_KERNEL_PAGE_WINDOW = XIS_RELEASE_VALUE;
    ks_atomic_store_release(&xis_release_armed, 1);
    (void)x86_page_window_release(window);
    xis_native_abort(0x4b34544c42524c32ULL);
#else
    if (!kernel_log(0, "KERNEL_IPI_SERVICE_READY")) xis_native_abort(0x4b344950494c4f47ULL);
#if KERNEL_TEST_AP_SERVICE == 4
    xis_native_abort(0x4b344950494e4d34ULL);
#else
    x86_kernel_stop(0x4b34495049504153ULL);
#endif
#endif
#endif
}
