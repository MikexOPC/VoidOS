#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include <stdio.h>
#include <stdlib.h>

XAT_RECORD xat_record;
X86_TASK_STACK xat_stacks[8];
X86_TASK_CONTEXT xat_contexts[8];
KERNEL_PHYSICAL_ALLOCATOR *xat_pool;
X86_AP_SNAPSHOT xat_arena;
uint32_t xat_mask;
XIS_NATIVE xis_native;
XP_PANIC_NATIVE xp_panic_native;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_AP_AUTH auth;
static X86_LAPIC_MAPPING lapic;
static X86_CPU_LOCAL owner;
#ifdef KERNEL_TEST_AP_CONCURRENT
static XAT_RECORD second_record;
XAT_RECORD *xat_cpu_record(void) { return owner.Index == 2 ? &second_record : &xat_record; }
#endif
static X86_TASK_CPU cpu;
static uint64_t entries[4][512], rsp;
static uint32_t checks, reads, corrupt_gdt, corrupt_idt, pinned = 1;

// ====== check_private ======

static void check_private(int value)
{ ++checks; if (!value) { fprintf(stderr,"AP gate check %u\n",checks); exit(1); } }

// ====== mocked_cpu_and_page_views ======

const X86_CPU_LOCAL *x86_percpu_current_local(void) { return &owner; }
uint64_t x86_kernel_stack_pointer(void) { return rsp; }
int x86_scheduler_native_gate(void) { return 1; }
uint64_t x86_task_stack_base(uint32_t index)
{ return index < 8 ? X86_TASK_STACK_GUARD + ((uint64_t)index * 18 + 1) * 4096 : 0; }
const X86_AP_AUTH *x86_ap_owner(void) { return &auth; }
volatile uint64_t *x86_scheduler_code_entries(uint32_t level, uint64_t target)
{ (void)target; ++reads; return level < 4 ? entries[level] : NULL; }
void x86_kernel_segments_read(X86_GDT_POINTER *output)
{
    *output = (X86_GDT_POINTER){{39,0}};
    uint64_t address = X86_AP_METADATA(owner.Index);
    for (uint32_t byte = 0; byte < 8; ++byte) output->Bytes[byte + 2] = (unsigned char)(address >> (byte * 8));
    if (corrupt_gdt) output->Bytes[0] = 38;
}
void x86_exception_read_idt(X86_KERNEL_IDTR *output)
{ *output = xis_native.Idtr; if (corrupt_idt) output->Bytes[0] ^= 1; }
uint32_t x86_ap_leaf(uint64_t entry, uint64_t address)
{
    uint64_t base = X86_AP_STACK_BASE(owner.Index);
    return address >= base && address < base + 65536 &&
        (entry & ~0x60ULL) == ((auth.Data.Base + address - base) | 3 | (1ULL << 63)) ? 10 : 0;
}
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *allocator, const KERNEL_PHYSICAL_LEASE *lease)
{ (void)allocator; (void)lease; return (int)pinned; }
int x86_task_stack_mapping_valid(const X86_TASK_STACK *stack, uint32_t mode)
{ (void)stack; return mode == 1; }
int x86_scheduler_code_valid(uint64_t target) { (void)target; return 1; }
uint64_t ks_atomic_load_relaxed(const uint64_t *value) { return *value; }
void ks_atomic_store_release(uint64_t *value, uint64_t next) { *value = next; }

// ====== test_cpu_private ======

static void test_cpu_private(uint32_t actor_index)
{
    owner.Index = actor_index;
    uint32_t task = (actor_index - 1) * 2;
    uint64_t root = 0x10000, base = X86_AP_STACK_BASE(owner.Index);
    pool.Self = (uint64_t)(uintptr_t)&pool; pool.PhysicalBits = 40;
    xat_pool = &pool; xat_record.Root = root;
    owner.Self = (uint64_t)(uintptr_t)&owner; owner.Index = actor_index; owner.State = X86_PERCPU_ACTIVE;
    xp_panic_native.Contexts[actor_index] = &owner; xp_panic_native.Ids[actor_index] = actor_index;
    lapic.Physical = 0xfee00000; xp_panic_native.Lapic = &lapic;
    xis_native.Idtr.Bytes[0] = 255; xis_native.Idtr.Bytes[1] = 15;
    auth.Self = (uint64_t)(uintptr_t)&auth; auth.Root = root; auth.Ready = 1; auth.PhysicalBits = 40; auth.Cpus = 4;
    auth.Data = (KERNEL_PHYSICAL_LEASE){0}; auth.Tables = (KERNEL_PHYSICAL_LEASE){0};
    auth.Data.Id = 1; auth.Data.Owner = pool.Self; auth.Data.Base = 0x200000; auth.Data.Pages = 87;
    auth.Tables.Id = 2; auth.Tables.Owner = pool.Self; auth.Tables.Base = 0x300000; auth.Tables.Pages = 3;
    xat_arena = (X86_AP_SNAPSHOT){auth.Self,auth.Data.Base,87,1,auth.Tables.Base,2,4,3};
    cpu.cr0 = 0x80010001; cpu.cr3 = root; cpu.cr4 = 0x200; cpu.apic_base = 0xfee00800;
    cpu.max_leaf = 1; cpu.apic_id = actor_index; cpu.features = (1U << 9) | (1U << 24) | (1U << 25) | (1U << 26);
    for (uint32_t level = 0; level < 3; ++level)
        entries[level][(base >> (39 - level * 9)) & 511] = (auth.Tables.Base + level * 4096ULL) | 3;
    uint32_t at = (uint32_t)((base >> 12) & 511);
    for (uint32_t page = 0; page < 16; ++page) entries[3][at + page] = (auth.Data.Base + page * 4096ULL) | 3 | (1ULL << 63);
    check_private(xad_offer(actor_index,0,1,root));
    check_private(xad_claim(actor_index,actor_index,root) == 1 && xad_admit(actor_index,actor_index,1,root));
    check_private(!xat_ap_gate(&pool,root,&cpu));
    check_private(xad_transition(actor_index,0,1,root,0) && xad_transition(actor_index,actor_index,1,root,1));
    check_private(xat_ap_gate(&pool,root,&cpu));
    for (uint32_t level = 0; level < 3; ++level) {
        uint32_t where = (uint32_t)((base >> (39 - level * 9)) & 511);
        uint64_t original = entries[level][where];
        entries[level][where] = 0; reads = 0;
        check_private(!xat_ap_gate(&pool,root,&cpu) && reads == level + 1);
        entries[level][where] = original;
    }
    for (uint32_t page = 0; page < 16; ++page) {
        entries[3][at + page] ^= 4;
        check_private(!xat_ap_gate(&pool,root,&cpu));
        entries[3][at + page] ^= 4;
    }
    entries[3][at - 1] = 1; check_private(!xat_ap_gate(&pool,root,&cpu)); entries[3][at - 1] = 0;
    entries[3][at + 16] = 1; check_private(!xat_ap_gate(&pool,root,&cpu)); entries[3][at + 16] = 0;
    corrupt_gdt = 1; check_private(!xat_ap_gate(&pool,root,&cpu)); corrupt_gdt = 0;
    corrupt_idt = 1; check_private(!xat_ap_gate(&pool,root,&cpu)); corrupt_idt = 0;
    cpu.apic_base |= 256; check_private(!xat_ap_gate(&pool,root,&cpu)); cpu.apic_base &= ~256ULL;
    auth.Ready = 0; check_private(!xat_ap_gate(&pool,root,&cpu)); auth.Ready = 1;
    auth.Data.Owner ^= 8; check_private(!xat_ap_gate(&pool,root,&cpu)); auth.Data.Owner ^= 8;
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_prepare(); check_private(local != NULL);
    local->phase = 1; rsp = X86_AP_STACK_FLOOR(actor_index);
    check_private(xat_dispatcher_gate());
    check_private(xat_stack_authority(&local->dispatcher,base,65536));
    pinned = 0; check_private(!xat_stack_authority(&local->dispatcher,base,65536)); pinned = 1;
    local->phase = 2; local->current = task; local->generation = 7;
    xat_record.Generations[task] = 7;
    xat_stacks[task].self = (uint64_t)(uintptr_t)&xat_stacks[task]; xat_stacks[task].pool = &pool;
    xat_stacks[task].root = root; xat_stacks[task].generation = 7; xat_stacks[task].stage = X86_TASK_STACK_READY;
    xat_stacks[task].lease.Id = 3; xat_stacks[task].lease.Owner = pool.Self; xat_stacks[task].lease.Pages = 16;
    xat_stacks[task].index = task;
    rsp = x86_task_stack_base(task) + 8192;
    check_private(xat_task_valid());
    xat_stacks[task].lease.Pages = 32; check_private(!xat_task_valid()); xat_stacks[task].lease.Pages = 16;
    check_private(!xat_irq_frame(actor_index,(const X86_IRQ_FRAME *)(uintptr_t)(x86_task_stack_base(task) + 8)));
    check_private(!xat_irq_frame(0,NULL));
#ifdef KERNEL_TEST_AP_CONCURRENT
    uint32_t foreign = task ^ 2;
    xat_stacks[foreign] = xat_stacks[task];
    xat_stacks[foreign].self = (uint64_t)(uintptr_t)&xat_stacks[foreign];
    xat_stacks[foreign].index = foreign;
    xat_record.Generations[foreign] = 7;
    local->current = foreign; rsp = x86_task_stack_base(foreign) + 8192;
    check_private(!xat_task_valid());
    check_private(!xat_stack_authority(&xat_contexts[foreign],x86_task_stack_base(foreign),65536));
    local->current = task; rsp = x86_task_stack_base(task) + 8192;
    check_private(!xat_irq_frame(actor_index == 1 ? 2 : 1,NULL));
#endif
    check_private(xad_fail(actor_index,0,1,root) && !xat_ap_gate(&pool,root,&cpu));
}

// ====== main ======

int main(void)
{
    check_private(xad_configure(4,0x10000,0));
    for (uint32_t actor_index = 1; actor_index <= XAT_APS; ++actor_index) test_cpu_private(actor_index);
    printf("AP_GATE_PASS: %u checks, real C validator/Rust protocol, privileged reads and mapping views MOCK\n",checks);
    return 0;
}
