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
    if (!x86_page_window_allocate_lease(process_rt.Window, pages, lease_out)) {
        return 0;
    }
    BOOT_U64 base = lease_out->Base;
    BOOT_U64 branch = PTE_PRESENT | PTE_WRITE | PTE_USER;
    const volatile BOOT_U64 *root = x86_window_entries(0);
    BOOT_U64 entry = (base + PAGE_BYTES) | branch;
    if (!x86_page_window_write_lease(process_rt.Window, lease_out, 0, &entry, sizeof(entry))) goto fail;
    entry = root[SLOT_IMAGE];
    if (!x86_page_window_write_lease(process_rt.Window, lease_out, (BOOT_U64)SLOT_IMAGE * 8ULL, &entry, sizeof(entry))) goto fail;
    entry = root[X86_LAYOUT_WINDOWS_SLOT];
    if (!x86_page_window_write_lease(process_rt.Window, lease_out, (BOOT_U64)X86_LAYOUT_WINDOWS_SLOT * 8ULL, &entry, sizeof(entry))) goto fail;
    if (process_rt.Permanent) {
        entry = root[X86_LAYOUT_HEAP_SLOT];
        if (!x86_page_window_write_lease(process_rt.Window, lease_out, (BOOT_U64)X86_LAYOUT_HEAP_SLOT * 8ULL, &entry, sizeof(entry))) goto fail;
        entry = root[X86_LAYOUT_AP_SLOT];
        if (!x86_page_window_write_lease(process_rt.Window, lease_out, (BOOT_U64)X86_LAYOUT_AP_SLOT * 8ULL, &entry, sizeof(entry))) goto fail;
    }
    entry = (base + 2ULL * PAGE_BYTES) | branch;
    if (!x86_page_window_write_lease(process_rt.Window, lease_out, 512ULL * 8ULL, &entry, sizeof(entry))) goto fail;
    entry = (base + 3ULL * PAGE_BYTES) | branch;
    if (!x86_page_window_write_lease(process_rt.Window, lease_out, (1024ULL + 2ULL) * 8ULL, &entry, sizeof(entry))) goto fail;
    for (BOOT_U64 index = 0; index < plan->Count; ++index) {
        BOOT_U64 lease = TABLE_PAGES + index;
        BOOT_U64 flags = PTE_PRESENT | PTE_USER;
        if (plan->Pages[index].Flags & 1ULL) flags |= PTE_WRITE;
        if (!(plan->Pages[index].Flags & 2ULL)) flags |= PTE_NX;
        entry = (base + lease * PAGE_BYTES) | flags;
        if (!x86_page_window_write_lease(process_rt.Window, lease_out,
                (BOOT_U64)PT_OFFSET(plan->Pages[index].Virt) * 8ULL, &entry, sizeof(entry))) {
            goto fail;
        }
        if (plan->Pages[index].FileBytes) {
            if (!x86_page_window_write_lease(process_rt.Window, lease_out,
                    lease * PAGE_BYTES,
                    image->Bytes + plan->Pages[index].FileOffset,
                    plan->Pages[index].FileBytes)) {
                goto fail;
            }
        }
    }
    for (BOOT_U64 index = 0; index < STACK_PAGES; ++index) {
        BOOT_U64 lease = TABLE_PAGES + plan->Count + index;
        entry = (base + lease * PAGE_BYTES) | branch | PTE_NX;
        if (!x86_page_window_write_lease(process_rt.Window, lease_out,
                (BOOT_U64)PT_OFFSET(STACK_BASE + index * PAGE_BYTES) * 8ULL, &entry, sizeof(entry))) {
            goto fail;
        }
    }
    return 1;
fail:
    if (!x86_page_window_free_lease(process_rt.Window, lease_out)) {
        process_die("PROCESS_RELEASE_FAILED");
    }
    return 0;
}

// ====== reap ======

static int reap(X86_PROCESS_CPU *cpu, KERNEL_PHYSICAL_LEASE *lease)
{
    if (__readcr3() != cpu->KernelRoot || !roots_stable() || STACK_WORD != STACK_CANARY) return 0;
    if (!x86_page_window_free_lease(process_rt.Window, lease)) process_die("PROCESS_RELEASE_FAILED");
    cpu->UserRoot = 0;
    return x86_page_window_idle_valid(process_rt.Window);
}

// ====== turn_finish ======

static void turn_finish(X86_PROCESS_CPU *cpu, X86_PROCESS_RESULT *result)
{
    process_timer_quiesce();
    if (cpu->ExitKind == X86_PROCESS_FAULT) {
        result->Kind = X86_PROCESS_FAULT;
        result->Vector = cpu->Fault[0];
        result->Error = cpu->Fault[1];
        result->Rip = cpu->Fault[2];
        result->Cr2 = cpu->FaultCr2;
    }
    __writemsr(MSR_FS_BASE, 0);
    __writemsr(MSR_KERNEL_GS, 0);
    cpu->Active = 0;
    cpu->LeaveRequested = 0;
    cpu->Current = 0;
}

// ====== x86_process_run ======

int x86_process_run(const X86_PROCESS_IMAGE *image, X86_PROCESS_RESULT *result)
{
    static PROCESS_PLAN plan;
    BOOT_U64 budget;
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || !image || !result) return 0;
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
    cpu->UserRoot = process_rt.Held.Base;
    advance(2);
    cpu->Current = result;
    cpu->WrittenBytes = 0;
    cpu->ExitKind = 0;
    cpu->LeaveRequested = 0;
    cpu->Active = 1;
    advance(3);
    process_timer_start();
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_probe_armed) process_probe_send();
#endif
    x86_process_enter(plan.Entry, STACK_TOP, 0);
    advance(4);
    turn_finish(cpu, result);
    if (!reap(cpu, &process_rt.Held) || !kernel_physical_budget_matches(process_rt.Window->Pool, budget)) {
        process_die(roots_stable() ? "PROCESS_REAP_FAILED" : "PROCESS_ROOT_CHANGED");
    }
    advance(5);
    return result->Kind != 0;
}

// ====== x86_process_slot_prepare_budgeted ======

int x86_process_slot_prepare_budgeted(X86_PROCESS_SLOT *slot, const X86_PROCESS_IMAGE *image,
    BOOT_U32 max_image_pages, BOOT_U32 cpu_budget_ms)
{
    static PROCESS_PLAN plan;
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || cpu->Current || !slot || slot->Lease.Id || !image ||
        !max_image_pages || max_image_pages > MAX_IMAGE_PAGES || !cpu_budget_ms ||
        x86_process_plan_budgeted(image->Bytes, image->Length, max_image_pages, &plan)) {
        return 0;
    }
    BOOT_U32 ticks = kernel_clock_ms(cpu->BudgetClock, cpu_budget_ms);
    if (ticks == UINT32_MAX || !map_image(image, &plan, &slot->Lease)) return 0;
    slot->KernelRoot = cpu->KernelRoot;
    slot->ImageEntry = process_rt.ExpectedImage;
    slot->WindowEntry = process_rt.ExpectedWindows;
    slot->Entry = plan.Entry;
    slot->RemainingTicks = ticks;
    slot->RemainingIrqs = x86_process_budget_initial();
    return 1;
}

// ====== x86_process_slot_prepare ======

int x86_process_slot_prepare(X86_PROCESS_SLOT *slot, const X86_PROCESS_IMAGE *image)
{
    return x86_process_slot_prepare_budgeted(slot, image, MAX_IMAGE_PAGES, PROCESS_BUDGET_MS);
}

// ====== x86_process_slot_step ======

int x86_process_slot_step(X86_PROCESS_SLOT *slot, X86_PROCESS_RESULT *result,
    IPC_CHANNEL *channel, BOOT_U64 endpoint, int renew_budget)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || cpu->Current || !slot || !slot->Lease.Id || !result || result->Kind ||
        (!!channel != !!endpoint) || (slot->Waiting && !channel)) return 0;
    if (slot->KernelRoot != cpu->KernelRoot || slot->ImageEntry != process_rt.ExpectedImage ||
        slot->WindowEntry != process_rt.ExpectedWindows || !roots_stable()) return 0;
    if (renew_budget) {
        BOOT_U32 ticks = kernel_clock_ms(cpu->BudgetClock, PROCESS_BUDGET_MS);
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
    cpu->UserRoot = slot->Lease.Base;
    cpu->Current = result;
    cpu->WrittenBytes = slot->Written;
    cpu->BudgetLeft = slot->RemainingIrqs;
    cpu->ExitKind = 0;
    cpu->Yielded = 0;
    cpu->Waiting = 0;
    cpu->LeaveRequested = 0;
    cpu->Active = 1;
    cpu->YieldFrame = slot->Frame;
    cpu->IpcChannel = channel;
    cpu->IpcEndpoint = endpoint;
    if (!kernel_deadline_start(cpu->BudgetClock, slot->RemainingTicks, &cpu->BudgetDeadline)) process_die("PROCESS_CLOCK_FAILED");
    process_timer_arm();
    if (slot->Yields) x86_process_resume(slot->Frame);
    else x86_process_enter(slot->Entry, STACK_TOP, endpoint);
    BOOT_U32 elapsed = kernel_clock_elapsed(cpu->BudgetClock, cpu->BudgetDeadline.Start);
    cpu->YieldFrame = 0;
    cpu->IpcChannel = 0;
    cpu->IpcEndpoint = 0;
    turn_finish(cpu, result);
    if (__readcr3() != cpu->KernelRoot || !roots_stable() || STACK_WORD != STACK_CANARY)
        process_die("PROCESS_ROOT_CHANGED");
    slot->Written = cpu->WrittenBytes;
    slot->RemainingIrqs = cpu->BudgetLeft;
    if (cpu->Yielded) {
        ++slot->Yields;
        slot->Waiting = cpu->Waiting;
        if (elapsed >= slot->RemainingTicks) result->Kind = X86_PROCESS_BUDGET;
        else slot->RemainingTicks -= elapsed;
    }
    return 1;
}

// ====== x86_process_slot_step_managed ======

int x86_process_slot_step_managed(X86_PROCESS_SLOT *slot, X86_PROCESS_RESULT *result,
    KERNEL_PROCESS_TABLE *table, BOOT_U64 pid, BOOT_U64 initial_arg,
    BOOT_U64 *wait_handle_io, int renew_budget)
{
    uint64_t saved_if = ks_irq_save();
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || cpu->Current || !slot || !slot->Lease.Id || !result || result->Kind ||
        !table || !pid || !wait_handle_io) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (slot->KernelRoot != cpu->KernelRoot || slot->ImageEntry != process_rt.ExpectedImage ||
        slot->WindowEntry != process_rt.ExpectedWindows || !roots_stable()) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (renew_budget) {
        BOOT_U32 ticks = kernel_clock_ms(cpu->BudgetClock, PROCESS_BUDGET_MS);
        if (ticks == UINT32_MAX) {
            ks_irq_restore(saved_if);
            return 0;
        }
        slot->RemainingTicks = ticks;
        slot->RemainingIrqs = x86_process_budget_initial();
    }
    if (slot->Waiting) {
        BOOT_U64 ready = kernel_ipc_receive_ready(table, pid, *wait_handle_io);
        if (ready == KERNEL_IPC_WOULD_BLOCK) {
            ks_irq_restore(saved_if);
            return 0;
        }
        GPR(slot->Frame, GPR_RAX) = ready;
        slot->Waiting = 0;
        *wait_handle_io = 0;
    }
    cpu->UserRoot = slot->Lease.Base;
    cpu->Current = result;
    cpu->WrittenBytes = slot->Written;
    cpu->BudgetLeft = slot->RemainingIrqs;
    cpu->ExitKind = 0;
    cpu->Yielded = 0;
    cpu->Waiting = 0;
    cpu->WaitHandle = 0;
    cpu->LeaveRequested = 0;
    cpu->Active = 1;
    cpu->YieldFrame = slot->Frame;
    cpu->IpcChannel = 0;
    cpu->IpcEndpoint = 0;
    cpu->Table = table;
    cpu->Pid = pid;
    if (!kernel_deadline_start(cpu->BudgetClock, slot->RemainingTicks, &cpu->BudgetDeadline)) process_die("PROCESS_CLOCK_FAILED");
    process_timer_arm();
    if (slot->Yields) x86_process_resume(slot->Frame);
    else x86_process_enter(slot->Entry, STACK_TOP, initial_arg);
    BOOT_U32 elapsed = kernel_clock_elapsed(cpu->BudgetClock, cpu->BudgetDeadline.Start);
    cpu->YieldFrame = 0;
    cpu->Table = 0;
    cpu->Pid = 0;
    turn_finish(cpu, result);
    BOOT_U64 canary_addr = cpu->SyscallTop - X86_KERNEL_SYSCALL_BYTES;
    if (__readcr3() != cpu->KernelRoot || !roots_stable() || *(volatile BOOT_U64 *)canary_addr != STACK_CANARY)
        process_die("PROCESS_ROOT_CHANGED");
    slot->Written = cpu->WrittenBytes;
    slot->RemainingIrqs = cpu->BudgetLeft;
    if (cpu->Yielded) {
        ++slot->Yields;
        slot->Waiting = cpu->Waiting;
        *wait_handle_io = cpu->Waiting ? cpu->WaitHandle : 0;
        if (elapsed >= slot->RemainingTicks) result->Kind = X86_PROCESS_BUDGET;
        else slot->RemainingTicks -= elapsed;
    }
    ks_irq_restore(saved_if);
    return 1;
}

// ====== x86_process_slot_reap ======

int x86_process_slot_reap(X86_PROCESS_SLOT *slot)
{
    X86_PROCESS_CPU *cpu = x86_process_cpu_current();
    if (!process_rt.Opened || !cpu || cpu->Current || !slot || !slot->Lease.Id || !reap(cpu, &slot->Lease)) return 0;
    *slot = (X86_PROCESS_SLOT){0};
    return 1;
}
