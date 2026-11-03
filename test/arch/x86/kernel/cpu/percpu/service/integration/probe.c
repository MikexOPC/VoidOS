#include "probe.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/configs/boot_profile.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/stack/main.h"

// ====== number_private ======

static void number_private(const char *name, uint64_t value) {
    if (!kernel_text(name) || !kernel_number(value) || !kernel_text("\n")) xis_native_abort(0x4b344950494c4f47ULL);
}

// ====== xis_service_probe ======

__declspec(noreturn) void xis_service_probe(const KERNEL_STARTUP *startup) {
#ifdef XIS_REMAP_TEST
    X86_PAGE_WINDOW *window = &startup->Memory->Window;
#else
    (void)startup;
#endif
    if (ks_irq_state() || xis_native_index() != 0 || xis_native.Count < 2) xis_native_abort(0x4b34495049425350ULL);
    number_private("KERNEL_IPI_NATIVE: ", (uint64_t)&xis_native);
    number_private("KERNEL_IPI_IDTR: ", (uint64_t)&xis_native.Idtr);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 0, XIS_READY)) xis_native_abort(0x4b34495049524459ULL);
#if KERNEL_TEST_AP_SERVICE == 5
    if (xis_publish(1, 0) != 1 || !xis_native_send(1) || !xis_native_wait(1, 2, 1))
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
            if (generation != round || !xis_native_send(index)) xis_native_abort(0x4b3449504953454eULL);
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
        if (xis_value(index, XIS_ACK) != 100 || ks_atomic_load_acquire(&xis_native.Cpus[index].Delivered) != 100 ||
            ks_atomic_load_acquire(&xis_native.Cpus[index].Eois) != 100 ||
            !ks_atomic_load_acquire(&xis_native.Cpus[index].Spurious)) xis_native_abort(0x4b34495049454e44ULL);
        number_private("KERNEL_IPI_ACK: ", xis_value(index, XIS_ACK));
    }
#ifdef XIS_REMAP_TEST
    if (!kernel_physical_release_after_unmap(window->Pool, &old) || !x86_page_window_release(window))
        xis_native_abort(0x4b34544c4252454cULL);
    for (uint32_t index = 1; index < xis_native.Count; ++index)
        if (!xis_native_wait(index, 1, XIS_READ_ROUND + 1)) xis_native_abort(0x4b34544c42444f4eULL);
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
        if (!xis_native_wait(index, 1, XIS_READ_ROUND + 2)) xis_native_abort(0x4b34544c42524734ULL);
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
        if (!xis_native_wait(index, 1, XIS_READ_ROUND + 3)) xis_native_abort(0x4b34544c42485033ULL);
#endif
    if (!kernel_log(0, "KERNEL_IPI_REMAP_VERIFIED")) xis_native_abort(0x4b34544c4252454cULL);
#endif
    if (!kernel_log(0, "KERNEL_IPI_SERVICE_READY")) xis_native_abort(0x4b344950494c4f47ULL);
#if KERNEL_TEST_AP_SERVICE == 4
    xis_native_abort(0x4b344950494e4d34ULL);
#else
    x86_kernel_stop(0x4b34495049504153ULL);
#endif
#endif
}
