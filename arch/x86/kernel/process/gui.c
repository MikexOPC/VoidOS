#include "gui.h"
#include "manager.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/mm/heap/binding.h"
#include "kernel/mm/physical/holdings.h"

// User SHM virtual window: PD[128..511] = [0x10000000, 0x40000000) (768 MiB, 2 MiB-aligned slots).
#define X86_GUI_PD_START       128U
#define X86_GUI_PD_END         512U
#define X86_GUI_PD_BYTE_OFFSET (1024ULL * 8ULL)
#define X86_GUI_MAX_RECTS_CALL 16U

typedef struct {
    uint64_t               ShmId;
    KERNEL_PHYSICAL_LEASE  Lease;
    uint32_t               Active;
    uint32_t               Reserved;
} X86_GUI_SHM_LEASE;

typedef struct {
    uint64_t               Pid;
    uint64_t               ShmId;
    KERNEL_PHYSICAL_LEASE  PtLease;
    uint32_t               PdIndex;
    uint32_t               PdCount;
    uint32_t               Active;
    uint32_t               Reserved;
} X86_GUI_PT_LEASE;

static KERNEL_GUI x86_gui_state;
static KERNEL_DISPLAY x86_gui_display;
static KERNEL_HOLDING x86_gui_holding;
static uint64_t x86_gui_held_pages;
static X86_GUI_SHM_LEASE *x86_gui_shm_leases;
static uint32_t x86_gui_shm_lease_cap;
static X86_GUI_PT_LEASE *x86_gui_pt_leases;
static uint32_t x86_gui_pt_lease_cap;
static uint32_t x86_gui_ready;
static const KERNEL_CLOCK *x86_gui_clock;
static KERNEL_STOPWATCH x86_gui_uptime;
static uint64_t x86_gui_uptime_base_ms;
static uint64_t x86_gui_uptime_ms;
static uint64_t x86_gui_clock_failed;
// BSP-only row staging buffer (8 KiB) for fast 64-bit framebuffer blits up to 2048 px wide.
static uint32_t x86_gui_blit_buf[2048];

// ====== x86_gui_clock_tick ======

void x86_gui_clock_tick(void)
{
    if (!x86_gui_clock)
        return;
    if (!kernel_stopwatch_sample(x86_gui_clock, &x86_gui_uptime) ||
        x86_gui_uptime.Lost
    ) {
        // 24-bit ACPI PM timer wrapped while idle/paused: re-anchor monotonically instead of poisoning display_info.
        x86_gui_uptime_base_ms = ks_atomic_load_acquire(&x86_gui_uptime_ms);
        if (!kernel_stopwatch_start(x86_gui_clock, &x86_gui_uptime)) {
            ks_atomic_store_release(&x86_gui_clock_failed, 1);
        }
        return;
    }
    uint64_t ticks = x86_gui_uptime.Total;
    uint64_t hz = x86_gui_clock->Hz;
    uint64_t milliseconds = x86_gui_uptime_base_ms + ticks / hz * 1000ULL + ticks % hz * 1000ULL / hz;
    ks_atomic_store_release(&x86_gui_uptime_ms, milliseconds);
}


// ====== update_holdings_private ======

static int update_holdings_private(int64_t delta_pages)
{
    if (delta_pages < 0 && (uint64_t)(-delta_pages) > x86_gui_held_pages)
        return 0;
    uint64_t next = delta_pages >= 0
        ? (x86_gui_held_pages + (uint64_t)delta_pages)
        : (x86_gui_held_pages - (uint64_t)(-delta_pages));
    if (!kernel_holdings_update(&x86_gui_holding, next))
        return 0;
    x86_gui_held_pages = next;
    return 1;
}

// ====== store_shm_lease_private ======

static int store_shm_lease_private(uint64_t shm_id, const KERNEL_PHYSICAL_LEASE *lease)
{
    KERNEL_HEAP *heap = kernel_heap();
    if (!heap || !shm_id || !lease)
        return 0;
    for (uint32_t idx = 0; idx < x86_gui_shm_lease_cap; ++idx) {
        if (!x86_gui_shm_leases[idx].Active) {
            x86_gui_shm_leases[idx].ShmId = shm_id;
            x86_gui_shm_leases[idx].Lease = *lease;
            x86_gui_shm_leases[idx].Active = 1;
            return 1;
        }
    }
    if (x86_gui_shm_lease_cap >= KERNEL_GUI_SHM_MAX_OBJECTS)
        return 0;
    uint32_t next_cap = x86_gui_shm_lease_cap ? x86_gui_shm_lease_cap * 2U : KERNEL_GUI_SHM_INIT_OBJECTS;
    if (next_cap > KERNEL_GUI_SHM_MAX_OBJECTS)
        next_cap = KERNEL_GUI_SHM_MAX_OBJECTS;
    X86_GUI_SHM_LEASE *grown = kernel_heap_alloc(heap, (size_t)next_cap * sizeof(*grown), 16);
    if (!grown)
        return 0;
    for (uint32_t idx = 0; idx < next_cap; ++idx) {
        grown[idx].Active = 0;
        grown[idx].ShmId = 0;
    }
    if (x86_gui_shm_leases) {
        for (uint32_t idx = 0; idx < x86_gui_shm_lease_cap; ++idx)
            grown[idx] = x86_gui_shm_leases[idx];
        (void)kernel_heap_free(heap, x86_gui_shm_leases);
    }
    uint32_t slot = x86_gui_shm_lease_cap;
    x86_gui_shm_leases = grown;
    x86_gui_shm_lease_cap = next_cap;
    x86_gui_shm_leases[slot].ShmId = shm_id;
    x86_gui_shm_leases[slot].Lease = *lease;
    x86_gui_shm_leases[slot].Active = 1;
    return 1;
}

// ====== find_shm_lease_private ======

static X86_GUI_SHM_LEASE *find_shm_lease_private(uint64_t shm_id)
{
    for (uint32_t idx = 0; idx < x86_gui_shm_lease_cap; ++idx) {
        if (x86_gui_shm_leases[idx].Active && x86_gui_shm_leases[idx].ShmId == shm_id)
            return &x86_gui_shm_leases[idx];
    }
    return NULL;
}

// ====== free_shm_backing_private ======

static void free_shm_backing_private(uint64_t shm_id)
{
    X86_GUI_SHM_LEASE *entry = find_shm_lease_private(shm_id);
    if (!entry)
        return;
    KERNEL_PHYSICAL_LEASE lease = entry->Lease;
    uint64_t pages = lease.Pages;
    entry->Active = 0;
    entry->ShmId = 0;
    uint64_t saved_if = ks_irq_save();
    if (!x86_page_window_free_lease(process_rt.Window, &lease))
        process_die("GUI_SHM_FREE_FAILED");
    ks_irq_restore(saved_if);
    if (!update_holdings_private(-(int64_t)pages))
        process_die("GUI_HOLDING_UNDERFLOW");
}

// ====== store_pt_lease_private ======

static int store_pt_lease_private(uint64_t pid, uint64_t shm_id,
    const KERNEL_PHYSICAL_LEASE *pt_lease, uint32_t pd_index, uint32_t pd_count)
{
    KERNEL_HEAP *heap = kernel_heap();
    if (!heap || !pid || !shm_id || !pt_lease)
        return 0;
    for (uint32_t idx = 0; idx < x86_gui_pt_lease_cap; ++idx) {
        if (!x86_gui_pt_leases[idx].Active) {
            x86_gui_pt_leases[idx].Pid = pid;
            x86_gui_pt_leases[idx].ShmId = shm_id;
            x86_gui_pt_leases[idx].PtLease = *pt_lease;
            x86_gui_pt_leases[idx].PdIndex = pd_index;
            x86_gui_pt_leases[idx].PdCount = pd_count;
            x86_gui_pt_leases[idx].Active = 1;
            return 1;
        }
    }
    if (x86_gui_pt_lease_cap >= KERNEL_GUI_SHM_MAX_MAPPINGS)
        return 0;
    uint32_t next_cap = x86_gui_pt_lease_cap ? x86_gui_pt_lease_cap * 2U : KERNEL_GUI_SHM_INIT_MAPPINGS;
    if (next_cap > KERNEL_GUI_SHM_MAX_MAPPINGS)
        next_cap = KERNEL_GUI_SHM_MAX_MAPPINGS;
    X86_GUI_PT_LEASE *grown = kernel_heap_alloc(heap, (size_t)next_cap * sizeof(*grown), 16);
    if (!grown)
        return 0;
    for (uint32_t idx = 0; idx < next_cap; ++idx)
        grown[idx].Active = 0;
    if (x86_gui_pt_leases) {
        for (uint32_t idx = 0; idx < x86_gui_pt_lease_cap; ++idx)
            grown[idx] = x86_gui_pt_leases[idx];
        (void)kernel_heap_free(heap, x86_gui_pt_leases);
    }
    uint32_t slot = x86_gui_pt_lease_cap;
    x86_gui_pt_leases = grown;
    x86_gui_pt_lease_cap = next_cap;
    x86_gui_pt_leases[slot].Pid = pid;
    x86_gui_pt_leases[slot].ShmId = shm_id;
    x86_gui_pt_leases[slot].PtLease = *pt_lease;
    x86_gui_pt_leases[slot].PdIndex = pd_index;
    x86_gui_pt_leases[slot].PdCount = pd_count;
    x86_gui_pt_leases[slot].Active = 1;
    return 1;
}

// ====== map_shm_into_slot_private ======

static int map_shm_into_slot_private(uint64_t pid, X86_PROCESS_SLOT *slot,
    uint64_t shm_id, uint32_t pages, uint64_t phys_base, int writable, uint64_t *user_virt_out)
{
    if (!slot || !pages || !phys_base || !user_virt_out)
        return 0;
    if (kernel_gui_shm_find_mapping(&x86_gui_state, pid, shm_id, NULL, NULL, NULL))
        return 0;
    uint32_t pd_count = (pages + 511U) / 512U;
    uint64_t saved_if = ks_irq_save();
    BOOT_U64 pd_entries[X86_GUI_PD_END - X86_GUI_PD_START];
    if (!x86_page_window_read_lease(process_rt.Window, &slot->Lease,
        X86_GUI_PD_BYTE_OFFSET + (BOOT_U64)X86_GUI_PD_START * 8ULL,
        pd_entries, sizeof(pd_entries))) {
        ks_irq_restore(saved_if);
        return 0;
    }
    uint32_t found = X86_GUI_PD_END;
    uint32_t run = 0;
    for (uint32_t idx = 0; idx < X86_GUI_PD_END - X86_GUI_PD_START; ++idx) {
        if (pd_entries[idx] == 0) {
            if (++run == pd_count) {
                found = X86_GUI_PD_START + idx + 1U - pd_count;
                break;
            }
        } else {
            run = 0;
        }
    }
    if (found == X86_GUI_PD_END) {
        ks_irq_restore(saved_if);
        return 0;
    }
    KERNEL_PHYSICAL_LEASE pt_lease = {0};
    if (!x86_page_window_allocate_lease(process_rt.Window, pd_count, &pt_lease)) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (!update_holdings_private((int64_t)pd_count)) {
        (void)x86_page_window_free_lease(process_rt.Window, &pt_lease);
        ks_irq_restore(saved_if);
        return 0;
    }
    // Intel SDM Vol. 3A (December 2024), section 5.6.1: leaf R/W bit 1 controls user writes.
    BOOT_U64 pte_flags = PTE_PRESENT | PTE_USER | PTE_NX;
    if (writable)
        pte_flags |= PTE_WRITE;
    BOOT_U64 branch_flags = PTE_PRESENT | PTE_WRITE | PTE_USER;
    for (uint32_t pd_off = 0; pd_off < pd_count; ++pd_off) {
        BOOT_U64 pt_buf[512];
        for (uint32_t entry_idx = 0; entry_idx < 512U; ++entry_idx) {
            uint32_t page = pd_off * 512U + entry_idx;
            pt_buf[entry_idx] = (page < pages)
                ? ((phys_base + (BOOT_U64)page * PAGE_BYTES) | pte_flags)
                : 0ULL;
        }
        if (!x86_page_window_write_lease(process_rt.Window, &pt_lease,
            (BOOT_U64)pd_off * PAGE_BYTES, pt_buf, sizeof(pt_buf))) {
            (void)x86_page_window_free_lease(process_rt.Window, &pt_lease);
            (void)update_holdings_private(-(int64_t)pd_count);
            ks_irq_restore(saved_if);
            return 0;
        }
    }
    for (uint32_t idx = 0; idx < pd_count; ++idx) {
        BOOT_U64 pde = (pt_lease.Base + (BOOT_U64)idx * PAGE_BYTES) | branch_flags;
        if (!x86_page_window_write_lease(process_rt.Window, &slot->Lease,
            X86_GUI_PD_BYTE_OFFSET + (BOOT_U64)(found + idx) * 8ULL, &pde, sizeof(pde))) {
            for (uint32_t undo = 0; undo < idx; ++undo) {
                BOOT_U64 zero = 0;
                (void)x86_page_window_write_lease(process_rt.Window, &slot->Lease,
                    X86_GUI_PD_BYTE_OFFSET + (BOOT_U64)(found + undo) * 8ULL, &zero, sizeof(zero));
            }
            (void)x86_page_window_free_lease(process_rt.Window, &pt_lease);
            (void)update_holdings_private(-(int64_t)pd_count);
            ks_irq_restore(saved_if);
            return 0;
        }
    }
    ks_irq_restore(saved_if);
    uint64_t user_virt = (uint64_t)found * (2ULL * 1024ULL * 1024ULL);
    if (!store_pt_lease_private(pid, shm_id, &pt_lease, found, pd_count) ||
        !kernel_gui_shm_map_record(&x86_gui_state, pid, shm_id, user_virt)) {
        for (uint32_t idx = 0; idx < x86_gui_pt_lease_cap; ++idx) {
            if (x86_gui_pt_leases[idx].Active && x86_gui_pt_leases[idx].Pid == pid
                && x86_gui_pt_leases[idx].ShmId == shm_id
            )
                x86_gui_pt_leases[idx].Active = 0;
        }
        saved_if = ks_irq_save();
        for (uint32_t idx = 0; idx < pd_count; ++idx) {
            BOOT_U64 zero = 0;
            (void)x86_page_window_write_lease(process_rt.Window, &slot->Lease,
                X86_GUI_PD_BYTE_OFFSET + (BOOT_U64)(found + idx) * 8ULL, &zero, sizeof(zero));
        }
        (void)x86_page_window_free_lease(process_rt.Window, &pt_lease);
        (void)update_holdings_private(-(int64_t)pd_count);
        ks_irq_restore(saved_if);
        return 0;
    }
    *user_virt_out = user_virt;
    return 1;
}

// ====== unmap_pt_from_slot_private ======

static void unmap_pt_from_slot_private(uint64_t pid, X86_PROCESS_SLOT *slot, uint64_t shm_id)
{
    for (uint32_t idx = 0; idx < x86_gui_pt_lease_cap; ++idx) {
        if (!x86_gui_pt_leases[idx].Active ||
            x86_gui_pt_leases[idx].Pid != pid ||
            x86_gui_pt_leases[idx].ShmId != shm_id)
            continue;
        KERNEL_PHYSICAL_LEASE pt_lease = x86_gui_pt_leases[idx].PtLease;
        uint64_t pt_pages = pt_lease.Pages;
        uint32_t pd_index = x86_gui_pt_leases[idx].PdIndex;
        uint32_t pd_count = x86_gui_pt_leases[idx].PdCount;
        x86_gui_pt_leases[idx].Active = 0;
        uint64_t saved_if = ks_irq_save();
        if (slot) {
            for (uint32_t k = 0; k < pd_count; ++k) {
                BOOT_U64 zero = 0;
                (void)x86_page_window_write_lease(process_rt.Window, &slot->Lease,
                    X86_GUI_PD_BYTE_OFFSET + (BOOT_U64)(pd_index + k) * 8ULL, &zero, sizeof(zero));
            }
        }
        if (!x86_page_window_free_lease(process_rt.Window, &pt_lease))
            process_die("GUI_PT_FREE_FAILED");
        ks_irq_restore(saved_if);
        if (!update_holdings_private(-(int64_t)pt_pages))
            process_die("GUI_PT_HOLDING_UNDERFLOW");
        break;
    }
}

// ====== refresh_display_geometry_private ======

static int refresh_display_geometry_private(BOOT_U64 *fb_base_out,
    BOOT_U32 *width_out, BOOT_U32 *height_out, BOOT_U32 *stride_out, BOOT_U32 *format_out)
{
    BOOT_U64 base = 0;
    BOOT_U32 w = 0, h = 0, stride = 0, fmt = 0;
    if (!x86_fb_kernel_surface_info(&base, &w, &h, &stride, &fmt))
        return 0;
    (void)kernel_gui_set_display_geometry(&x86_gui_state, w, h, stride, fmt);
    if (fb_base_out) *fb_base_out = base;
    if (width_out) *width_out = w;
    if (height_out) *height_out = h;
    if (stride_out) *stride_out = stride;
    if (format_out) *format_out = fmt;
    return 1;
}

// ====== x86_gui_init ======

int x86_gui_init(void)
{
    if (x86_gui_ready)
        return 1;
    KERNEL_HEAP *heap = kernel_heap();
    if (!heap || !kernel_gui_init(&x86_gui_state, heap))
        return 0;
    x86_gui_held_pages = 0;
    if (!kernel_holdings_register(&x86_gui_holding, 0)) {
        (void)kernel_gui_destroy(&x86_gui_state);
        return 0;
    }
    kernel_gui_global_set(&x86_gui_state);
    (void)refresh_display_geometry_private(NULL, NULL, NULL, NULL, NULL);
    x86_gui_ready = 1;
    return 1;
}

// ====== x86_gui_bind_display ======

void x86_gui_bind_display(KERNEL_DISPLAY display)
{
    x86_gui_display = display;
    if (x86_gui_ready)
        (void)refresh_display_geometry_private(NULL, NULL, NULL, NULL, NULL);
}

// ====== x86_gui_release_owned ======

void x86_gui_release_owned(uint64_t pid, X86_PROCESS_SLOT *slot)
{
    if (!x86_gui_ready || !pid)
        return;
    uint64_t disp_shm = 0;
    (void)kernel_gui_display_release(&x86_gui_state, pid, &disp_shm);
    uint64_t shm_id = 0, user_virt = 0, phys_base = 0;
    uint32_t pages = 0;
    int freed = 0;
    while (kernel_gui_shm_pop_owned(&x86_gui_state, pid, &shm_id, &user_virt, &pages, &phys_base, &freed)) {
        if (user_virt)
            unmap_pt_from_slot_private(pid, slot, shm_id);
        if (freed)
            free_shm_backing_private(shm_id);
    }
}

// ====== read_cmos_reg_private ======

static uint8_t read_cmos_reg_private(uint8_t reg)
{
    // Motorola MC146818 / Intel ICH LPC CMOS RTC: port 0x70 = index (bit 7 NMI=0), port 0x71 = data.
    x86_write_port(0x70U, reg & 0x7FU);
    return x86_read_port(0x71U);
}

// ====== bcd_to_bin_private ======

static uint8_t bcd_to_bin_private(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

// ====== read_cmos_rtc_packed_private ======

static uint64_t read_cmos_rtc_packed_private(void)
{
    // MC146818 Status Register A (0x0A) bit 7 = UIP (Update In Progress).
    // Status Register B (0x0B) bit 2 = DM (0=BCD, 1=binary), bit 1 = 24/12h (1=24h).
    uint8_t sec = 0, min = 0, hour = 0, day = 0, month = 0, year = 0, reg_b = 0;
    uint8_t sec2 = 0, min2 = 0, hour2 = 0, day2 = 0, month2 = 0, year2 = 0;
    int consistent = 0;
    for (uint32_t attempt = 0; attempt < 8U; ++attempt) {
        uint32_t spin = 0;
        while ((read_cmos_reg_private(0x0AU) & 0x80U) != 0U && spin < 10000U)
            ++spin;
        if (spin >= 10000U)
            return 0ULL;
        sec = read_cmos_reg_private(0x00U);
        min = read_cmos_reg_private(0x02U);
        hour = read_cmos_reg_private(0x04U);
        day = read_cmos_reg_private(0x07U);
        month = read_cmos_reg_private(0x08U);
        year = read_cmos_reg_private(0x09U);
        reg_b = read_cmos_reg_private(0x0BU);
        sec2 = read_cmos_reg_private(0x00U);
        min2 = read_cmos_reg_private(0x02U);
        hour2 = read_cmos_reg_private(0x04U);
        day2 = read_cmos_reg_private(0x07U);
        month2 = read_cmos_reg_private(0x08U);
        year2 = read_cmos_reg_private(0x09U);
        if (sec == sec2 && min == min2 && hour == hour2 &&
            day == day2 && month == month2 && year == year2) {
            consistent = 1;
            break;
        }
    }
    if (!consistent)
        return 0ULL;
    uint8_t pm = (uint8_t)(hour & 0x80U);
    hour = (uint8_t)(hour & 0x7FU);
    if ((reg_b & 0x04U) == 0U) {
        sec = bcd_to_bin_private(sec);
        min = bcd_to_bin_private(min);
        hour = bcd_to_bin_private(hour);
        day = bcd_to_bin_private(day);
        month = bcd_to_bin_private(month);
        year = bcd_to_bin_private(year);
    }
    if ((reg_b & 0x02U) == 0U) {
        if (pm)
            hour = (uint8_t)((hour % 12U) + 12U);
        else if (hour == 12U)
            hour = 0U;
    }
    uint16_t full_year = (uint16_t)(2000U + year);
    if (month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || min > 59U || sec > 59U)
        return 0ULL;
    return ((uint64_t)full_year << 48)
        | ((uint64_t)month << 40)
        | ((uint64_t)day << 32)
        | ((uint64_t)hour << 24)
        | ((uint64_t)min << 16)
        | ((uint64_t)sec << 8)
        | 1ULL;
}

// ====== x86_gui_sys_display_info ======

BOOT_U64 x86_gui_sys_display_info(X86_PROCESS_CPU *cpu, BOOT_U64 info_ptr)
{
    if (!x86_gui_ready || !cpu)
        return RESULT_UNKNOWN;
    if (x86_process_copy_range(info_ptr, sizeof(KERNEL_DISPLAY_INFO)) != 1)
        return RESULT_RANGE;
    if (!refresh_display_geometry_private(NULL, NULL, NULL, NULL, NULL))
        return RESULT_UNKNOWN;
    if (cpu == &x86_process_cpus[0]) {
        uint64_t saved_if = ks_irq_save();
        if (!x86_gui_clock) {
            x86_gui_clock = cpu->BudgetClock;
            if (!kernel_stopwatch_start(x86_gui_clock, &x86_gui_uptime)) {
                x86_gui_clock = NULL;
                ks_irq_restore(saved_if);
                return RESULT_UNKNOWN;
            }
        }
        x86_gui_clock_tick();
        ks_irq_restore(saved_if);
    }
    if (ks_atomic_load_acquire(&x86_gui_clock_failed))
        return RESULT_UNKNOWN;
    uint64_t uptime_ms = ks_atomic_load_acquire(&x86_gui_uptime_ms);
    KERNEL_DISPLAY_INFO info = {0};
    if (!kernel_gui_display_info(&x86_gui_state, uptime_ms, &info))
        return RESULT_UNKNOWN;
    uint64_t rtc_if = ks_irq_save();
    info.RtcPacked = read_cmos_rtc_packed_private();
    ks_irq_restore(rtc_if);
    if (x86_process_copy_user(cpu, (void *)info_ptr, &info, info_ptr, sizeof(info), 1))
        return RESULT_RANGE;
    return 0;
}

// ====== x86_gui_sys_display_acquire ======

BOOT_U64 x86_gui_sys_display_acquire(X86_PROCESS_CPU *cpu, BOOT_U64 out_ptr)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready || !cpu || !cpu->Table || !cpu->Pid)
        return RESULT_UNKNOWN;
    if (x86_process_copy_range(out_ptr, sizeof(X86_DISPLAY_ACQUIRE_OUT)) != 1)
        return RESULT_RANGE;
    KERNEL_PROCESS_BUDGET budget = {0};
    if (!kernel_process_get_budget(cpu->Table, cpu->Pid, &budget) ||
        !(budget.Flags & KERNEL_BUDGET_FLAG_DISPLAY))
        return RESULT_QUOTA;
    BOOT_U32 w = 0, h = 0, stride = 0, fmt = 0;
    if (!refresh_display_geometry_private(NULL, &w, &h, &stride, &fmt))
        return RESULT_UNKNOWN;
    uint64_t bytes = (uint64_t)stride * (uint64_t)h * 4ULL;
    uint32_t pages = (uint32_t)((bytes + 4095ULL) / 4096ULL);
    if (!pages || pages > KERNEL_GUI_SHM_MAX_PAGES)
        return RESULT_LENGTH;
    X86_PROCESS_SLOT *slot = x86_process_slot_for_pid(cpu->Pid);
    if (!slot)
        return RESULT_UNKNOWN;
    KERNEL_PHYSICAL_LEASE lease = {0};
    uint64_t saved_if = ks_irq_save();
    int alloc_ok = x86_page_window_allocate_lease(process_rt.Window, pages, &lease);
    ks_irq_restore(saved_if);
    if (!alloc_ok)
        return RESULT_QUOTA;
    if (!update_holdings_private((int64_t)pages)) {
        saved_if = ks_irq_save();
        (void)x86_page_window_free_lease(process_rt.Window, &lease);
        ks_irq_restore(saved_if);
        return RESULT_QUOTA;
    }
    uint64_t shm_id = 0;
    if (!kernel_gui_shm_create(&x86_gui_state, cpu->Pid, pages, lease.Base, 1, &shm_id) ||
        !store_shm_lease_private(shm_id, &lease)) {
        if (shm_id)
            (void)kernel_gui_shm_release_unmapped(&x86_gui_state, shm_id, NULL, NULL, NULL);
        saved_if = ks_irq_save();
        (void)x86_page_window_free_lease(process_rt.Window, &lease);
        ks_irq_restore(saved_if);
        (void)update_holdings_private(-(int64_t)pages);
        return RESULT_QUOTA;
    }
    uint64_t user_virt = 0;
    if (!map_shm_into_slot_private(cpu->Pid, slot, shm_id, pages, lease.Base, 1, &user_virt)) {
        (void)kernel_gui_shm_release_unmapped(&x86_gui_state, shm_id, NULL, NULL, NULL);
        free_shm_backing_private(shm_id);
        return RESULT_QUOTA;
    }
    if (!kernel_gui_display_acquire(&x86_gui_state, cpu->Pid, shm_id)) {
        int freed = 0;
        (void)kernel_gui_shm_unmap_record(&x86_gui_state, cpu->Pid, shm_id, NULL, NULL, NULL, &freed);
        unmap_pt_from_slot_private(cpu->Pid, slot, shm_id);
        if (freed)
            free_shm_backing_private(shm_id);
        return RESULT_QUOTA;
    }
    X86_DISPLAY_ACQUIRE_OUT out = {0};
    out.ShmId = shm_id;
    out.UserVirt = user_virt;
    out.Width = w;
    out.Height = h;
    out.Stride = stride;
    out.Format = fmt;
    if (x86_process_copy_user(cpu, (void *)out_ptr, &out, out_ptr, sizeof(out), 1)) {
        x86_gui_release_owned(cpu->Pid, slot);
        return RESULT_RANGE;
    }
    return 0;
}

// ====== x86_gui_sys_display_present ======

BOOT_U64 x86_gui_sys_display_present(X86_PROCESS_CPU *cpu, BOOT_U64 rects_ptr, BOOT_U64 count)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready || !cpu || !cpu->Pid)
        return RESULT_UNKNOWN;
    if (!count || count > X86_GUI_MAX_RECTS_CALL)
        return RESULT_LENGTH;
    KERNEL_DISPLAY_INFO info = {0};
    if (!kernel_gui_display_info(&x86_gui_state, 0, &info) || info.OwnerPid != cpu->Pid)
        return RESULT_QUOTA;
    BOOT_U64 byte_len = count * sizeof(KERNEL_DISPLAY_RECT);
    if (x86_process_copy_range(rects_ptr, byte_len) != 1)
        return RESULT_RANGE;
    KERNEL_DISPLAY_RECT rects[X86_GUI_MAX_RECTS_CALL];
    if (x86_process_copy_user(cpu, rects, (const void *)rects_ptr, rects_ptr, byte_len, 0))
        return RESULT_RANGE;
    BOOT_U64 fb_base = 0;
    BOOT_U32 fb_w = 0, fb_h = 0, fb_stride = 0, fb_fmt = 0;
    if (!refresh_display_geometry_private(&fb_base, &fb_w, &fb_h, &fb_stride, &fb_fmt))
        return RESULT_UNKNOWN;
    X86_GUI_SHM_LEASE *backbuffer = find_shm_lease_private(info.BackbufferShmId);
    if (!backbuffer)
        return RESULT_UNKNOWN;
    for (BOOT_U64 idx = 0; idx < count; ++idx) {
        if (!kernel_gui_rect_valid(fb_w, fb_h, &rects[idx]))
            return RESULT_RANGE;
    }
    // Fast row blit: stage up to 2048 pixels (full 1080p row) per lease read and write 64-bit pairs to VRAM.
    volatile uint32_t *fb_pixels = (volatile uint32_t *)(uintptr_t)fb_base;
    for (BOOT_U64 idx = 0; idx < count; ++idx) {
        const KERNEL_DISPLAY_RECT *r = &rects[idx];
        for (uint32_t row = 0; row < r->Height; ++row) {
            uint32_t y = r->Y + row;
            uint32_t col = 0;
            while (col < r->Width) {
                uint32_t chunk = r->Width - col;
                if (chunk > 2048U)
                    chunk = 2048U;
                uint64_t byte_off = ((uint64_t)y * (uint64_t)fb_stride + (uint64_t)(r->X + col)) * 4ULL;
                uint64_t saved_if = ks_irq_save();
                int ok = x86_page_window_read_lease(process_rt.Window, &backbuffer->Lease,
                    byte_off, x86_gui_blit_buf, (BOOT_U64)chunk * 4ULL);
                ks_irq_restore(saved_if);
                if (!ok)
                    return RESULT_UNKNOWN;
                volatile uint32_t *dst32 = fb_pixels + (size_t)y * (size_t)fb_stride + (size_t)(r->X + col);
                uint32_t p = 0;
                if ((((uintptr_t)dst32) & 7U) != 0U && p < chunk) {
                    dst32[0] = x86_gui_blit_buf[0];
                    ++p;
                }
                volatile uint64_t *dst64 = (volatile uint64_t *)(dst32 + p);
                const uint32_t *src32 = x86_gui_blit_buf + p;
                uint32_t pairs = (chunk - p) >> 1;
                for (uint32_t q = 0; q < pairs; ++q) {
                    uint64_t packed = (uint64_t)src32[q * 2U] | ((uint64_t)src32[q * 2U + 1U] << 32);
                    dst64[q] = packed;
                }
                p += pairs * 2U;
                if (p < chunk)
                    dst32[p] = x86_gui_blit_buf[p];
                col += chunk;
            }
        }
        if (x86_gui_display.Ops && x86_gui_display.Ops->Rect) {
            if (!x86_gui_display.Ops->Rect(x86_gui_display.Self, r->X, r->Y, r->Width, r->Height))
                return RESULT_UNKNOWN;
        }
    }
    return 0;
}

// ====== x86_gui_sys_input_poll ======

BOOT_U64 x86_gui_sys_input_poll(X86_PROCESS_CPU *cpu, BOOT_U64 event_ptr)
{
    if (!x86_gui_ready || !cpu || !cpu->Pid)
        return RESULT_UNKNOWN;
    if (x86_process_copy_range(event_ptr, sizeof(KERNEL_INPUT_EVENT)) != 1)
        return RESULT_RANGE;
    KERNEL_INPUT_EVENT ev = {0};
    int status = kernel_gui_input_poll(&x86_gui_state, cpu->Pid, &ev);
    if (status < 0)
        return RESULT_QUOTA;
    if (status == 0)
        return 0;
    if (x86_process_copy_user(cpu, (void *)event_ptr, &ev, event_ptr, sizeof(ev), 1))
        return RESULT_RANGE;
    return 1;
}

// ====== x86_gui_sys_shm_create ======

BOOT_U64 x86_gui_sys_shm_create(X86_PROCESS_CPU *cpu, BOOT_U64 pages)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready || !cpu || !cpu->Pid)
        return RESULT_UNKNOWN;
    if (!pages || pages > KERNEL_GUI_SHM_MAX_PAGES)
        return RESULT_LENGTH;
    KERNEL_PHYSICAL_LEASE lease = {0};
    uint64_t saved_if = ks_irq_save();
    int alloc_ok = x86_page_window_allocate_lease(process_rt.Window, pages, &lease);
    ks_irq_restore(saved_if);
    if (!alloc_ok)
        return RESULT_QUOTA;
    if (!update_holdings_private((int64_t)pages)) {
        saved_if = ks_irq_save();
        (void)x86_page_window_free_lease(process_rt.Window, &lease);
        ks_irq_restore(saved_if);
        return RESULT_QUOTA;
    }
    uint64_t shm_id = 0;
    if (!kernel_gui_shm_create(&x86_gui_state, cpu->Pid, (uint32_t)pages, lease.Base, 0, &shm_id) ||
        !store_shm_lease_private(shm_id, &lease)) {
        if (shm_id)
            (void)kernel_gui_shm_release_unmapped(&x86_gui_state, shm_id, NULL, NULL, NULL);
        saved_if = ks_irq_save();
        (void)x86_page_window_free_lease(process_rt.Window, &lease);
        ks_irq_restore(saved_if);
        (void)update_holdings_private(-(int64_t)pages);
        return RESULT_QUOTA;
    }
    return shm_id;
}

// ====== x86_gui_sys_shm_map ======

BOOT_U64 x86_gui_sys_shm_map(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready || !cpu || !cpu->Pid || !shm_id)
        return RESULT_UNKNOWN;
    X86_PROCESS_SLOT *slot = x86_process_slot_for_pid(cpu->Pid);
    if (!slot)
        return RESULT_UNKNOWN;
    uint32_t pages = 0;
    uint64_t phys_base = 0;
    int writable = 0;
    if (!kernel_gui_shm_pin(&x86_gui_state, cpu->Pid, shm_id, &pages, &phys_base, &writable))
        return RESULT_RANGE;
    uint64_t user_virt = 0;
    int mapped = map_shm_into_slot_private(cpu->Pid, slot, shm_id, pages, phys_base,
        writable, &user_virt);
    int freed = 0;
    if (!kernel_gui_shm_unpin(&x86_gui_state, shm_id, &freed))
        kernel_panic("GUI_SHM_PIN_LOST");
    if (freed)
        free_shm_backing_private(shm_id);
    if (!mapped)
        return RESULT_QUOTA;
    return user_virt;
}

// ====== x86_gui_sys_shm_unmap ======

BOOT_U64 x86_gui_sys_shm_unmap(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready || !cpu || !cpu->Pid || !shm_id)
        return RESULT_UNKNOWN;
    X86_PROCESS_SLOT *slot = x86_process_slot_for_pid(cpu->Pid);
    if (!slot)
        return RESULT_UNKNOWN;
    int freed = 0;
    if (!kernel_gui_shm_unmap_record(&x86_gui_state, cpu->Pid, shm_id, NULL, NULL, NULL, &freed))
        return RESULT_RANGE;
    unmap_pt_from_slot_private(cpu->Pid, slot, shm_id);
    if (freed)
        free_shm_backing_private(shm_id);
    return 0;
}

// ====== x86_gui_sys_shm_publish ======

BOOT_U64 x86_gui_sys_shm_publish(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!x86_gui_ready ||
        !cpu ||
        !cpu->Pid
    )
        return RESULT_UNKNOWN;
    return kernel_gui_shm_publish(&x86_gui_state, cpu->Pid, shm_id) ? 0 : RESULT_RANGE;
}

// ====== x86_gui_sys_shm_view ======

BOOT_U64 x86_gui_sys_shm_view(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id, BOOT_U64 out_ptr)
{
    if (cpu != &x86_process_cpus[0])
        return RESULT_QUOTA;
    if (!cpu ||
        x86_process_copy_range(out_ptr, 16U) != 1
    )
        return RESULT_RANGE;
    BOOT_U64 address = x86_gui_sys_shm_map(cpu, shm_id);
    if (address >= RESULT_QUOTA)
        return address;
    uint32_t pages = 0;
    if (!kernel_gui_shm_find_mapping(&x86_gui_state, cpu->Pid, shm_id, NULL, &pages, NULL)) {
        (void)x86_gui_sys_shm_unmap(cpu, shm_id);
        return RESULT_UNKNOWN;
    }
    BOOT_U64 out[2] = {address, (BOOT_U64)pages * PAGE_BYTES};
    if (x86_process_copy_user(cpu, (void *)out_ptr, out, out_ptr, sizeof(out), 1)) {
        (void)x86_gui_sys_shm_unmap(cpu, shm_id);
        return RESULT_RANGE;
    }
    return 0;
}
