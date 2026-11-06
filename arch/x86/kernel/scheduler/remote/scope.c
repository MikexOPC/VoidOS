#include "scope.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"
#include "kernel/sync/internal.h"

static X86_AP_SCOPE bound;
static uint64_t active;

// ====== cpu_private ======

static uint32_t cpu_private(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner ? owner->Index : UINT32_MAX;
}

// ====== x86_ap_scope_bind ======

int x86_ap_scope_bind(const X86_AP_SCOPE *scope)
{
    if (active || !scope || !scope->Root || !scope->Token || !scope->Pool || !scope->Aps || scope->Aps >= 64 ||
        !x86_percpu_bsp()) return 0;
    bound = *scope;
    ks_atomic_store_release(&active,1);
    return 1;
}

// ====== x86_ap_scope_unbind ======

void x86_ap_scope_unbind(void)
{
    ks_atomic_store_release(&active,0);
    bound = (X86_AP_SCOPE){0};
}

// ====== x86_ap_scope ======

const X86_AP_SCOPE *x86_ap_scope(void) { return ks_atomic_load_acquire(&active) ? &bound : NULL; }

// ====== x86_ap_actor ======

int x86_ap_actor(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    const X86_AP_SCOPE *scope = x86_ap_scope();
    return scope && owner && owner->Index >= 1 && owner->Index <= scope->Aps &&
        owner == xp_panic_native.Contexts[owner->Index] && owner->State == X86_PERCPU_ACTIVE &&
        owner->Self == (uint64_t)(uintptr_t)owner &&
        xad_value(owner->Index,scope->Root) == (scope->Token << 3 | XAD_RUNNING);
}

// ====== x86_ap_gate ======

// Intel SDM 078US §§4.5/4.6: checked recursive parents, P0/RW1/US2/XD63.
// §11.4.4: APIC_BASE.BSP8=0, global enable11=1, x2APIC10=0 in this profile.
int x86_ap_gate(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, const X86_TASK_CPU *cpu)
{
    const X86_AP_SCOPE *scope = x86_ap_scope();
    if (!x86_ap_actor() || !scope || !cpu || !xp_panic_native.Lapic || pool != scope->Pool || root != scope->Root ||
        !x86_scheduler_ap_cpu_valid(cpu,root,xp_panic_native.Ids[cpu_private()]) ||
        (cpu->cr4 & ((1ULL << 7) | (1ULL << 17))) ||
        cpu->apic_base != (xp_panic_native.Lapic->Physical | 0x800ULL)) return 0;
    const X86_AP_SNAPSHOT *arena = &scope->Arena;
    const X86_AP_AUTH *auth = x86_ap_owner();
    if (!auth || auth->Self != arena->Owner || auth->Root != root || auth->Cpus != arena->Cpus ||
        auth->Ready != 1 || auth->Reserved || auth->PhysicalBits != pool->PhysicalBits ||
        auth->Data.Pages != arena->Pages || auth->Tables.Pages != arena->Tables ||
        auth->Data.Owner != pool->Self || auth->Tables.Owner != pool->Self ||
        auth->Data.Id != arena->DataId || auth->Data.Base != arena->Base ||
        auth->Tables.Base != arena->TableBase || auth->Tables.Id != arena->TableId) return 0;
    X86_GDT_POINTER gdtr;
    X86_KERNEL_IDTR idtr;
    if (xis_native.Idtr.Bytes[0] != 255 || xis_native.Idtr.Bytes[1] != 15) return 0;
    x86_kernel_segments_read(&gdtr); x86_exception_read_idt(&idtr);
    uint64_t gdt_base = 0;
    for (uint32_t byte = 0; byte < 8; ++byte) gdt_base |= (uint64_t)gdtr.Bytes[byte + 2] << (byte * 8);
    if (gdtr.Bytes[0] != 39 || gdtr.Bytes[1] || gdt_base != X86_AP_METADATA(cpu_private())) return 0;
    for (uint32_t byte = 0; byte < 10; ++byte)
        if (idtr.Bytes[byte] != xis_native.Idtr.Bytes[byte]) return 0;
    uint64_t base = X86_AP_STACK_BASE(cpu_private());
    for (uint32_t level = 0; level < 3; ++level) {
        volatile uint64_t *entries = x86_scheduler_code_entries(level,base);
        uint32_t at = (uint32_t)((base >> (39 - level * 9)) & 511);
        if (!entries || (entries[at] & ~0x60ULL) != ((arena->TableBase + level * 4096ULL) | 3)) return 0;
    }
    volatile uint64_t *leaves = x86_scheduler_code_entries(3,base);
    uint32_t at = (uint32_t)((base >> 12) & 511);
    if (!leaves || leaves[at - 1] || leaves[at + 16]) return 0;
    for (uint32_t page = 0; page < 16; ++page)
        if (x86_ap_leaf(leaves[at + page],base + page * 4096ULL) != 10) return 0;
    return 1;
}

// ====== x86_ap_dispatcher_gate ======

int x86_ap_dispatcher_gate(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    uint64_t rsp = x86_kernel_stack_pointer();
    return x86_ap_actor() && local && local->phase == 1 &&
        rsp >= X86_AP_STACK_FLOOR(cpu_private()) && rsp < X86_AP_FRAME(cpu_private()) - XIS_IDLE_FRAME_BYTES &&
        x86_scheduler_native_gate();
}

// ====== x86_ap_dispatcher_authority ======

int x86_ap_dispatcher_authority(void)
{
    const X86_AP_SCOPE *scope = x86_ap_scope();
    const X86_AP_AUTH *auth = x86_ap_owner();
    return scope && x86_ap_actor() && x86_scheduler_native_gate() && auth &&
        kernel_physical_is_pinned(scope->Pool,&auth->Data) == 1 &&
        kernel_physical_is_pinned(scope->Pool,&auth->Tables) == 1;
}
