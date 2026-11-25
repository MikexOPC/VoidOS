#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/gui/gui.h"
#include "kernel/mm/heap/heap.h"
#include "kernel/process/table.h"
#include "kernel/sync/internal.h"

#define TEST_HEAP_PAGES 512U

static KERNEL_LOCK_CONTEXT host_lock_context;
static uint64_t host_irq_flags = 0x200ULL;
static uint32_t host_clock_ticks = 1;

// ====== host_clock_read ======

static int host_clock_read(void *context, uint32_t *out)
{
    (void)context;
    *out = host_clock_ticks++;
    return 1;
}

// ====== ks_irq_save ======

uint64_t ks_irq_save(void)
{
    uint64_t previous = host_irq_flags;
    host_irq_flags = 0;
    return previous;
}

// ====== ks_irq_restore ======

void ks_irq_restore(uint64_t flags)
{
    host_irq_flags = flags;
}

// ====== ks_irq_state ======

uint64_t ks_irq_state(void)
{
    return host_irq_flags;
}

// ====== ks_pause ======

void ks_pause(void)
{
}

// ====== ks_fail_stop ======

__declspec(noreturn) void ks_fail_stop(const char *reason)
{
    fprintf(stderr, "HOST_FAIL_STOP: %s\n", reason ? reason : "(null)");
    abort();
}

// ====== kernel_lock_current_context ======

KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void)
{
    return &host_lock_context;
}

// ====== setup_heap ======

static void *setup_heap(KERNEL_HEAP *heap)
{
    KERNEL_LOCK_WAIT wait = { { host_clock_read, NULL, UINT32_MAX, 1000U }, 1000U, 10000U };
    assert(kernel_lock_context_initialize(&host_lock_context, &wait));
    void *backing = _aligned_malloc((size_t)TEST_HEAP_PAGES * 4096U, 4096U);
    assert(backing != NULL);
    memset(heap, 0, sizeof(*heap));
    assert(kernel_heap_init(heap, backing, TEST_HEAP_PAGES));
    return backing;
}

// ====== test_display_ownership_and_rects ======

static void test_display_ownership_and_rects(KERNEL_HEAP *heap)
{
    KERNEL_GUI gui;
    assert(kernel_gui_init(&gui, heap));

    KERNEL_DISPLAY_INFO info = {0};
    assert(!kernel_gui_display_info(&gui, 123, &info));
    assert(!kernel_gui_set_display_geometry(&gui, 0, 768, 1024, 1));
    assert(!kernel_gui_set_display_geometry(&gui, 1024, 768, 512, 1));
    assert(kernel_gui_set_display_geometry(&gui, 1024, 768, 1024, 1));
    assert(kernel_gui_display_info(&gui, 456, &info));
    assert(info.Width == 1024 && info.Height == 768 && info.Stride == 1024 && info.UptimeMs == 456);
    assert(info.OwnerPid == 0 && info.BackbufferShmId == 0);

    // Rect bounds and uint32_t overflow checks.
    KERNEL_DISPLAY_RECT ok_rect = { 10, 20, 100, 200 };
    KERNEL_DISPLAY_RECT edge_rect = { 0, 0, 1024, 768 };
    KERNEL_DISPLAY_RECT zero_rect = { 0, 0, 0, 10 };
    KERNEL_DISPLAY_RECT oob_rect = { 1000, 0, 25, 10 };
    KERNEL_DISPLAY_RECT ovf_rect = { 0xfffffff0U, 0, 32, 10 };
    assert(kernel_gui_rect_valid(1024, 768, &ok_rect));
    assert(kernel_gui_rect_valid(1024, 768, &edge_rect));
    assert(!kernel_gui_rect_valid(1024, 768, &zero_rect));
    assert(!kernel_gui_rect_valid(1024, 768, &oob_rect));
    assert(!kernel_gui_rect_valid(1024, 768, &ovf_rect));

    // Only a display-flagged SHM can be acquired; only one owner allowed.
    uint64_t regular_shm = 0, disp_shm = 0;
    assert(kernel_gui_shm_create(&gui, 10, 4, 0x100000ULL, 0, &regular_shm));
    assert(kernel_gui_shm_create(&gui, 10, 768, 0x200000ULL, 1, &disp_shm));
    assert(!kernel_gui_display_acquire(&gui, 10, regular_shm));
    assert(kernel_gui_display_acquire(&gui, 10, disp_shm));
    assert(!kernel_gui_display_acquire(&gui, 11, disp_shm));
    assert(kernel_gui_display_owner(&gui) == 10);

    // Non-owner cannot release.
    uint64_t released_shm = 0;
    assert(!kernel_gui_display_release(&gui, 11, &released_shm));
    assert(kernel_gui_display_release(&gui, 10, &released_shm));
    assert(released_shm == disp_shm);
    assert(kernel_gui_display_owner(&gui) == 0);
    assert(kernel_gui_take_console_restore(&gui) == 1);
    assert(kernel_gui_take_console_restore(&gui) == 0);

    assert(kernel_gui_destroy(&gui));
}

// ====== test_input_ring ======

static void test_input_ring(KERNEL_HEAP *heap)
{
    KERNEL_GUI gui;
    assert(kernel_gui_init(&gui, heap));
    assert(kernel_gui_set_display_geometry(&gui, 800, 600, 800, 1));

    // Input rejected while no display owner is active.
    assert(!kernel_gui_input_push_key(&gui, 0x1E, 'a', 0));
    uint64_t disp_shm = 0;
    assert(kernel_gui_shm_create(&gui, 20, 469, 0x300000ULL, 1, &disp_shm));
    assert(kernel_gui_display_acquire(&gui, 20, disp_shm));

    assert(kernel_gui_input_push_key(&gui, 0x1E, 'a', 1));
    assert(kernel_gui_input_push_mouse(&gui, -5, 12, 3));

    KERNEL_INPUT_EVENT ev = {0};
    // Non-owner poll must fail (-1).
    assert(kernel_gui_input_poll(&gui, 21, &ev) == -1);
    assert(kernel_gui_input_poll(&gui, 20, &ev) == 1);
    assert(ev.Kind == KERNEL_INPUT_KEY && ev.Code == 0x1E && ev.Value == 'a' && ev.Flags == 1);
    assert(kernel_gui_input_poll(&gui, 20, &ev) == 1);
    assert(ev.Kind == KERNEL_INPUT_MOUSE && ev.DeltaX == -5 && ev.DeltaY == 12 && ev.Flags == 3);
    assert(kernel_gui_input_poll(&gui, 20, &ev) == 0);

    // Fill ring to capacity and verify overflow drops cleanly without corrupting FIFO.
    for (uint32_t idx = 0; idx < KERNEL_GUI_INPUT_CAPACITY; ++idx)
        assert(kernel_gui_input_push_key(&gui, idx, idx, 0));
    assert(!kernel_gui_input_push_key(&gui, 999, 999, 0));
    assert(gui.InputDropped == 1);
    for (uint32_t idx = 0; idx < KERNEL_GUI_INPUT_CAPACITY; ++idx) {
        assert(kernel_gui_input_poll(&gui, 20, &ev) == 1);
        assert(ev.Code == idx);
    }
    assert(kernel_gui_input_poll(&gui, 20, &ev) == 0);

    assert(kernel_gui_destroy(&gui));
}

// ====== test_shm_lifecycle_and_growth ======

static void test_shm_lifecycle_and_growth(KERNEL_HEAP *heap)
{
    KERNEL_GUI gui;
    assert(kernel_gui_init(&gui, heap));

    // Grow past initial object capacity (16) and mapping capacity (32).
    uint64_t ids[40];
    for (uint32_t idx = 0; idx < 40; ++idx) {
        assert(kernel_gui_shm_create(&gui, 100, 4, 0x1000000ULL + (uint64_t)idx * 0x4000ULL, 0, &ids[idx]));
        assert(kernel_gui_shm_map_record(&gui, 100, ids[idx], 0x10000000ULL + (uint64_t)idx * 0x200000ULL));
    }
    assert(gui.ShmObjectCapacity >= 40 && gui.ShmMappingCapacity >= 40);

    uint64_t display = 0;
    assert(kernel_gui_set_display_geometry(&gui, 800, 600, 800, 1));
    assert(kernel_gui_shm_create(&gui, 101, 1, 0xa000000ULL, 1, &display));
    assert(kernel_gui_display_acquire(&gui, 101, display));
    assert(!kernel_gui_shm_map_record(&gui, 101, ids[0], 0x10000000ULL));
    assert(!kernel_gui_shm_publish(&gui, 102, ids[0]));
    assert(!kernel_gui_shm_publish(&gui, 101, display));
    assert(kernel_gui_shm_publish(&gui, 100, ids[0]));
    assert(!kernel_gui_shm_map_record(&gui, 102, ids[0], 0x10000000ULL));
    assert(!kernel_gui_shm_map_record(&gui, 100, ids[0], 0x20000000ULL));
    assert(kernel_gui_shm_map_record(&gui, 101, ids[0], 0x10000000ULL));

    // Unmapping from PID 100 keeps object alive for PID 101.
    uint64_t vaddr = 0, phys = 0;
    uint32_t pages = 0;
    int freed = 0;
    assert(kernel_gui_shm_unmap_record(&gui, 100, ids[0], &vaddr, &pages, &phys, &freed));
    assert(freed == 0 && pages == 4 && phys == 0x1000000ULL);
    assert(!kernel_gui_shm_unmap_record(&gui, 100, ids[0], &vaddr, &pages, &phys, &freed));

    // Unmapping from PID 101 drops refcount to 0 and frees object.
    assert(kernel_gui_shm_unmap_record(&gui, 101, ids[0], &vaddr, &pages, &phys, &freed));
    assert(freed == 1);

    // Also create one unmapped SHM by PID 100 to verify pop_owned cleans it up too.
    uint64_t unmapped_id = 0;
    assert(kernel_gui_shm_create(&gui, 100, 2, 0x9000000ULL, 0, &unmapped_id));

    uint32_t popped = 0;
    uint64_t shm_id = 0;
    while (kernel_gui_shm_pop_owned(&gui, 100, &shm_id, &vaddr, &pages, &phys, &freed)) {
        assert(freed == 1);
        ++popped;
    }
    assert(popped == 40);
    assert(gui.ShmObjectCount == 1 && gui.ShmMappingCount == 0);
    assert(kernel_gui_shm_release_unmapped(&gui, display, NULL, NULL, NULL));

    assert(kernel_gui_destroy(&gui));
}

// ====== test_shm_authorization_and_pin ======

static void test_shm_authorization_and_pin(KERNEL_HEAP *heap)
{
    KERNEL_GUI gui;
    assert(kernel_gui_init(&gui, heap));
    assert(kernel_gui_set_display_geometry(&gui, 800, 600, 800, 1));
    uint64_t display = 0, surface = 0;
    assert(kernel_gui_shm_create(&gui, 10, 1, 0x100000ULL, 1, &display));
    assert(kernel_gui_shm_create(&gui, 20, 2, 0x200000ULL, 0, &surface));
    uint32_t pages = 0;
    uint64_t phys = 0;
    int writable = 0, freed = 0;
    assert(!kernel_gui_shm_pin(&gui, 10, surface, &pages, &phys, &writable));
    assert(!kernel_gui_shm_publish(&gui, 20, surface));
    assert(kernel_gui_display_acquire(&gui, 10, display));
    assert(kernel_gui_shm_pin(&gui, 20, surface, &pages, &phys, &writable));
    assert(kernel_gui_shm_unpin(&gui, surface, &freed) && !freed);
    assert(kernel_gui_shm_lookup(&gui, surface, NULL, NULL));
    assert(kernel_gui_shm_map_record(&gui, 20, surface, 0x10000000ULL));
    assert(kernel_gui_shm_publish(&gui, 20, surface));
    assert(!kernel_gui_shm_pin(&gui, 30, surface, &pages, &phys, &writable));
    assert(kernel_gui_shm_pin(&gui, 20, surface, &pages, &phys, &writable));
    assert(writable == 1 && pages == 2 && phys == 0x200000ULL);
    assert(kernel_gui_shm_unpin(&gui, surface, &freed) && !freed);
    assert(kernel_gui_shm_pin(&gui, 10, surface, &pages, &phys, &writable));
    assert(writable == 0);
    assert(kernel_gui_shm_unmap_record(&gui, 20, surface, NULL, NULL, NULL, &freed));
    assert(!freed);
    assert(kernel_gui_shm_lookup(&gui, surface, NULL, NULL));
    assert(kernel_gui_shm_map_record(&gui, 10, surface, 0x10000000ULL));
    assert(kernel_gui_shm_unpin(&gui, surface, &freed) && !freed);
    assert(kernel_gui_display_release(&gui, 10, NULL));
    assert(!kernel_gui_shm_pin(&gui, 10, surface, &pages, &phys, &writable));
    assert(kernel_gui_shm_unmap_record(&gui, 10, surface, NULL, NULL, NULL, &freed));
    assert(freed && !kernel_gui_shm_lookup(&gui, surface, NULL, NULL));
    assert(kernel_gui_shm_release_unmapped(&gui, display, NULL, NULL, NULL));
    assert(kernel_gui_destroy(&gui));
}

// ====== test_budget_display_flag ======

static void test_budget_display_flag(void)
{
    KERNEL_PROCESS_BUDGET root = {0};
    kernel_process_budget_default(&root);
    assert((root.Flags & KERNEL_BUDGET_FLAG_DISPLAY) != 0);

    KERNEL_PROCESS_BUDGET child_no_disp = root;
    child_no_disp.Flags = KERNEL_BUDGET_FLAG_SPAWN;
    KERNEL_PROCESS_BUDGET derived = {0};
    assert(kernel_process_budget_derive(&root, &child_no_disp, &derived));
    assert((derived.Flags & KERNEL_BUDGET_FLAG_DISPLAY) == 0);

    // Child without DISPLAY cannot escalate back to DISPLAY.
    KERNEL_PROCESS_BUDGET escalate = derived;
    escalate.Flags = KERNEL_BUDGET_FLAG_DISPLAY;
    KERNEL_PROCESS_BUDGET out = {0};
    assert(!kernel_process_budget_derive(&derived, &escalate, &out));
}

// ====== test_rtc_calendar ======

static void test_rtc_calendar(void)
{
    int valid = 0;
    uint64_t y2k = kernel_gui_rtc_to_epoch(2000, 1, 1, 0, 0, 0, &valid);
    assert(valid && y2k == 946684800ULL);
    uint64_t packed = kernel_gui_epoch_to_rtc_packed(y2k);
    assert((packed & 1ULL) == 1ULL);
    assert((packed >> 48) == 2000ULL && ((packed >> 40) & 0xFFU) == 1U && ((packed >> 32) & 0xFFU) == 1U);
    assert(((packed >> 24) & 0xFFU) == 0U && ((packed >> 16) & 0xFFU) == 0U && ((packed >> 8) & 0xFFU) == 0U);

    // Leap day 2024-02-29 23:59:59 + 1s -> 2024-03-01 00:00:00.
    uint64_t leap = kernel_gui_rtc_to_epoch(2024, 2, 29, 23, 59, 59, &valid);
    assert(valid);
    uint64_t mar1 = kernel_gui_epoch_to_rtc_packed(leap + 1ULL);
    assert((mar1 >> 48) == 2024ULL && ((mar1 >> 40) & 0xFFU) == 3U && ((mar1 >> 32) & 0xFFU) == 1U);
    assert(((mar1 >> 24) & 0xFFU) == 0U && ((mar1 >> 16) & 0xFFU) == 0U && ((mar1 >> 8) & 0xFFU) == 0U);

    // Year rollover 2026-12-31 23:59:59 + 1s -> 2027-01-01 00:00:00.
    uint64_t nye = kernel_gui_rtc_to_epoch(2026, 12, 31, 23, 59, 59, &valid);
    assert(valid);
    uint64_t ny = kernel_gui_epoch_to_rtc_packed(nye + 1ULL);
    assert((ny >> 48) == 2027ULL && ((ny >> 40) & 0xFFU) == 1U && ((ny >> 32) & 0xFFU) == 1U);

    // Invalid dates must fail closed.
    assert(kernel_gui_rtc_to_epoch(2026, 2, 29, 12, 0, 0, &valid) == 0ULL && !valid);
    assert(kernel_gui_rtc_to_epoch(2100, 2, 29, 12, 0, 0, &valid) == 0ULL && !valid);
    assert(kernel_gui_rtc_to_epoch(2026, 4, 31, 12, 0, 0, &valid) == 0ULL && !valid);
    assert(kernel_gui_rtc_to_epoch(2026, 13, 1, 0, 0, 0, &valid) == 0ULL && !valid);
    assert(kernel_gui_rtc_to_epoch(2026, 1, 1, 24, 0, 0, &valid) == 0ULL && !valid);
}

// ====== main ======

int main(void)
{
    KERNEL_HEAP heap;
    void *backing = setup_heap(&heap);
    test_display_ownership_and_rects(&heap);
    test_input_ring(&heap);
    test_shm_lifecycle_and_growth(&heap);
    test_shm_authorization_and_pin(&heap);
    test_budget_display_flag();
    test_rtc_calendar();
    _aligned_free(backing);
    printf("KERNEL_GUI_HOST_OK\n");
    return 0;
}
