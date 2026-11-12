#include "internal.h"

// ====== advance ======

static void advance(BOOT_U32 next)
{
    if (!x86_process_state_next(process_rt.State, next)) process_die("PROCESS_STATE_INVALID");
    process_rt.State = next;
}

// ====== roots_stable ======

static int roots_stable(void)
{
    const volatile BOOT_U64 *root = x86_window_entries(0);
    return root[SLOT_IMAGE] == process_rt.ExpectedImage && root[X86_LAYOUT_WINDOWS_SLOT] == process_rt.ExpectedWindows;
}

// ====== map_image ======

static int map_image(const X86_PROCESS_IMAGE *image, const PROCESS_PLAN *plan, KERNEL_PHYSICAL_LEASE *lease_out)
{
    BOOT_U64 pages = TABLE_PAGES + plan->Count + STACK_PAGES;
    if (!x86_page_window_idle_valid(process_rt.Window) || !x86_page_window_acquire(process_rt.Window, pages)) return 0;
    volatile unsigned char *alias = x86_window_bytes();
    volatile BOOT_U64 *tables = (volatile BOOT_U64 *)alias;
    BOOT_U64 base = process_rt.Window->Lease.Base;
    BOOT_U64 branch = PTE_PRESENT | PTE_WRITE | PTE_USER;
    for (BOOT_U64 index = 0; index < pages * PAGE_BYTES; ++index) alias[index] = 0;
    tables[0] = (base + PAGE_BYTES) | branch;
    tables[SLOT_IMAGE] = x86_window_entries(0)[SLOT_IMAGE];
    tables[X86_LAYOUT_WINDOWS_SLOT] = x86_window_entries(0)[X86_LAYOUT_WINDOWS_SLOT];
    tables[512] = (base + 2 * PAGE_BYTES) | branch;
    tables[1024 + 2] = (base + 3 * PAGE_BYTES) | branch;
    for (BOOT_U64 index = 0; index < plan->Count; ++index) {
        BOOT_U64 lease = TABLE_PAGES + index;
        BOOT_U64 flags = PTE_PRESENT | PTE_USER;
        if (plan->Pages[index].Flags & 1) flags |= PTE_WRITE;
        if (!(plan->Pages[index].Flags & 2)) flags |= PTE_NX;
        tables[PT_OFFSET(plan->Pages[index].Virt)] = (base + lease * PAGE_BYTES) | flags;
        for (BOOT_U64 byte = 0; byte < plan->Pages[index].FileBytes; ++byte)
            alias[lease * PAGE_BYTES + byte] = image->Bytes[plan->Pages[index].FileOffset + byte];
    }
    for (BOOT_U64 index = 0; index < STACK_PAGES; ++index) {
        BOOT_U64 lease = TABLE_PAGES + plan->Count + index;
        tables[PT_OFFSET(STACK_BASE + index * PAGE_BYTES)] = (base + lease * PAGE_BYTES) | branch | PTE_NX;
    }
    if (!x86_page_window_park(process_rt.Window, lease_out)) process_die("PROCESS_PARK_FAILED");
    return 1;
}

// ====== reap ======

static int reap(KERNEL_PHYSICAL_LEASE *lease)
{
    if (__readcr3() != k6_kernel_root || !roots_stable() || STACK_WORD != STACK_CANARY) return 0;
    BOOT_U64 pages = lease->Pages;
    if (!x86_page_window_resume(process_rt.Window, lease)) process_die("PROCESS_RESUME_FAILED");
    volatile unsigned char *alias = x86_window_bytes();
    for (BOOT_U64 index = 0; index < pages * PAGE_BYTES; ++index) alias[index] = 0;
    if (!x86_page_window_release(process_rt.Window)) process_die("PROCESS_RELEASE_FAILED");
    k6_user_root = 0;
    return x86_page_window_idle_valid(process_rt.Window);
}

// ====== turn_finish ======

static void turn_finish(X86_PROCESS_RESULT *result)
{
    process_timer_quiesce();
    if (k6_exit_kind == X86_PROCESS_FAULT) {
        result->Kind = X86_PROCESS_FAULT;
        result->Vector = k6_fault[0];
        result->Error = k6_fault[1];
        result->Rip = k6_fault[2];
        result->Cr2 = k6_fault_cr2;
    }
    __writemsr(MSR_FS_BASE, 0);
    __writemsr(MSR_KERNEL_GS, 0);
    process_rt.Current = 0;
}

// ====== x86_process_run ======

int x86_process_run(const X86_PROCESS_IMAGE *image, X86_PROCESS_RESULT *result)
{
    static PROCESS_PLAN plan;
    BOOT_U64 budget;
    if (!process_rt.Opened || !image || !result) return 0;
    for (BOOT_U32 index = 0; index < 8; ++index) ((BOOT_U64 *)result)[index] = 0;
    process_rt.State = 0;
    advance(1);
    if (!kernel_physical_budget_read(process_rt.Window->Pool, &budget)) return 0;
    BOOT_U32 rejected = x86_process_plan(image->Bytes, image->Length, &plan);
    if (rejected) {
        result->Kind = X86_PROCESS_REJECTED;
        result->Code = rejected;
        advance(5);
        return kernel_physical_budget_matches(process_rt.Window->Pool, budget);
    }
    if (!map_image(image, &plan, &process_rt.Held)) return 0;
    k6_user_root = process_rt.Held.Base;
    advance(2);
    process_rt.Current = result;
    process_rt.WrittenBytes = 0;
    k6_exit_kind = 0;
    advance(3);
    process_timer_start();
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) process_probe_send();
#endif
    x86_process_enter(plan.Entry, STACK_TOP, 0);
    advance(4);
    turn_finish(result);
    if (!reap(&process_rt.Held) || !kernel_physical_budget_matches(process_rt.Window->Pool, budget)) {
        process_die(roots_stable() ? "PROCESS_REAP_FAILED" : "PROCESS_ROOT_CHANGED");
    }
    advance(5);
    return result->Kind != 0;
}

// ====== x86_process_slot_prepare ======

int x86_process_slot_prepare(X86_PROCESS_SLOT *slot, const X86_PROCESS_IMAGE *image)
{
    PROCESS_PLAN plan;
    if (!process_rt.Opened || process_rt.Current || !slot || slot->Lease.Id || !image ||
        x86_process_plan(image->Bytes, image->Length, &plan)) return 0;
    BOOT_U32 ticks = kernel_clock_ms(process_rt.BudgetClock, PROCESS_BUDGET_MS);
    if (ticks == UINT32_MAX || !map_image(image, &plan, &slot->Lease)) return 0;
    slot->KernelRoot = k6_kernel_root;
    slot->ImageEntry = process_rt.ExpectedImage;
    slot->WindowEntry = process_rt.ExpectedWindows;
    slot->Entry = plan.Entry;
    slot->RemainingTicks = ticks;
    slot->RemainingIrqs = x86_process_budget_initial();
    return 1;
}

// ====== x86_process_slot_step ======

int x86_process_slot_step(X86_PROCESS_SLOT *slot, X86_PROCESS_RESULT *result,
    IPC_CHANNEL *channel, BOOT_U64 endpoint, int renew_budget)
{
    if (!process_rt.Opened || process_rt.Current || !slot || !slot->Lease.Id || !result || result->Kind ||
        (!!channel != !!endpoint) || (slot->Waiting && !channel)) return 0;
    if (slot->KernelRoot != k6_kernel_root || slot->ImageEntry != process_rt.ExpectedImage ||
        slot->WindowEntry != process_rt.ExpectedWindows || !roots_stable()) return 0;
    if (renew_budget) {
        BOOT_U32 ticks = kernel_clock_ms(process_rt.BudgetClock, PROCESS_BUDGET_MS);
        if (ticks == UINT32_MAX) return 0;
        slot->RemainingTicks = ticks;
        slot->RemainingIrqs = x86_process_budget_initial();
    }
    if (slot->Waiting) {
        BOOT_U64 ready = x86_ipc_receive_ready(channel, endpoint, endpoint);
        if (ready == IPC_WOULD_BLOCK) return 0;
        GPR(slot->Frame, GPR_RAX) = ready;
        slot->Waiting = 0;
    }
    k6_user_root = slot->Lease.Base;
    process_rt.Current = result;
    process_rt.WrittenBytes = slot->Written;
    process_rt.BudgetLeft = slot->RemainingIrqs;
    k6_exit_kind = 0;
    process_rt.Yielded = 0;
    process_rt.Waiting = 0;
    process_rt.YieldFrame = slot->Frame;
    process_rt.IpcChannel = channel;
    process_rt.IpcEndpoint = endpoint;
    if (!kernel_deadline_start(process_rt.BudgetClock, slot->RemainingTicks, &process_rt.BudgetDeadline)) process_die("PROCESS_CLOCK_FAILED");
    process_timer_arm();
    if (slot->Yields) x86_process_resume(slot->Frame);
    else x86_process_enter(slot->Entry, STACK_TOP, endpoint);
    BOOT_U32 elapsed = kernel_clock_elapsed(process_rt.BudgetClock, process_rt.BudgetDeadline.Start);
    process_rt.YieldFrame = 0;
    process_rt.IpcChannel = 0;
    process_rt.IpcEndpoint = 0;
    turn_finish(result);
    if (__readcr3() != k6_kernel_root || !roots_stable() || STACK_WORD != STACK_CANARY)
        process_die("PROCESS_ROOT_CHANGED");
    slot->Written = process_rt.WrittenBytes;
    slot->RemainingIrqs = process_rt.BudgetLeft;
    if (process_rt.Yielded) {
        ++slot->Yields;
        slot->Waiting = process_rt.Waiting;
        if (elapsed >= slot->RemainingTicks) result->Kind = X86_PROCESS_BUDGET;
        else slot->RemainingTicks -= elapsed;
    }
    return 1;
}

// ====== x86_process_slot_reap ======

int x86_process_slot_reap(X86_PROCESS_SLOT *slot)
{
    if (!process_rt.Opened || process_rt.Current || !slot || !slot->Lease.Id || !reap(&slot->Lease)) return 0;
    *slot = (X86_PROCESS_SLOT){0};
    return 1;
}
