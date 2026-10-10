#include "arch/x86/kernel/scheduler/mapping/native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_TASK_STACK owners[8];
static X86_TASK_CONTEXT contexts[8];
static uint64_t entries[512], code[4][512];
static unsigned char data[65536];
static uint32_t checks, invalidations, denied, read_levels;
static uint32_t live_test;
static uint64_t rsp = X86_KERNEL_MAIN_BASE + 4096;

// ====== x86_scheduler_dispatcher_context_is ======

int x86_scheduler_dispatcher_context_is(const X86_TASK_CONTEXT *context)
{
    return context == &contexts[7];
}

// ====== check_private ======

static void check_at_private(int value, unsigned int line)
{
    if (!value) { fprintf(stderr,"Native mapper check %u failed at %u\n",checks + 1,line); exit(1); }
    ++checks;
}
#define check_private(value) check_at_private((value),__LINE__)

// ====== CPU_and_physical_mocks ======

// ====== x86_scheduler_cpu_capture ======

int x86_scheduler_cpu_capture(X86_TASK_CPU *output)
{
    X86_TASK_CPU cpu = {0};
    cpu.cr0 = (1ULL << 31) | (1ULL << 16) | 1; cpu.cr3 = 0x200000; cpu.cr4 = 1ULL << 9;
    cpu.apic_base = 1ULL << 8; cpu.max_leaf = 1; cpu.features = (1U << 9) | (7U << 24);
    *output = cpu; return 1;
}
// ====== x86_scheduler_cpu_permissions ======

uint64_t x86_scheduler_cpu_permissions(void) { return !denied; }
// ====== x86_exception_environment ======

BOOT_U64 x86_exception_environment(void) { return 1; }
// ====== x86_kernel_stack_pointer ======

BOOT_U64 x86_kernel_stack_pointer(void) { return rsp; }
// ====== x86_main_pointer ======

BOOT_U32 x86_main_pointer(BOOT_U64 pointer) { return pointer >= X86_KERNEL_MAIN_BASE && pointer < X86_KERNEL_MAIN_TOP; }
// ====== kernel_physical_validate ======

int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner) { return owner == &pool; }
// ====== kernel_physical_is_pinned ======

int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease)
{ return owner == &pool && (lease->Id == 1 || (live_test && lease->Id > 1 && lease->Id <= 3)); }
// ====== kernel_physical_allocate ======

int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *lease)
{ (void)owner; (void)pages; (void)lease; return 0; }
// ====== kernel_physical_pin ======

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin)
{ (void)owner; (void)lease; (void)pin; return 0; }
// ====== kernel_physical_release ======

int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease)
{ (void)owner; (void)lease; return 0; }

// ====== address_and_invalidation_mocks ======

// ====== x86_kernel_stack_branch ======

int x86_kernel_stack_branch(BOOT_U64 root, BOOT_U32 bits) { return root == 0x200000 && bits == 48; }
// ====== x86_stack_entries ======

volatile BOOT_U64 *x86_stack_entries(BOOT_U32 level) { return level == 3 ? entries : NULL; }
// ====== x86_invalidate_page ======

void x86_invalidate_page(BOOT_U64 address)
{
    check_private(address >= X86_TASK_STACK_GUARD && address < X86_TASK_STACK_GUARD + 8 * 18 * 4096ULL);
    ++invalidations;
}
// ====== x86_scheduler_native_bytes ======

volatile unsigned char *x86_scheduler_native_bytes(uint32_t index) { return index < 8 ? data : NULL; }
// ====== x86_scheduler_code_entries ======

volatile uint64_t *x86_scheduler_code_entries(uint32_t level, uint64_t target)
{
    check_private(target == 0x100000000 && level == read_levels++);
    return code[level];
}

// ====== map_corruption_private ======

static void map_corruption_private(uint32_t index)
{
    uint64_t before[512];
    uint32_t first = 320 + index * 18, offset, bit;
    X86_TASK_STACK *owner = &owners[index];
    owner->self = (uint64_t)(uintptr_t)owner; owner->root = 0x200000; owner->pool = &pool;
    owner->generation = 1; owner->index = index; owner->stage = X86_TASK_STACK_MAPPING;
    owner->lease.Base = 0x100000 + index * 65536ULL; owner->lease.Pages = 16;
    owner->lease.Id = 1; owner->lease.Owner = pool.Self;
    check_private(x86_task_stack_map(owner));
    check_private(invalidations == 18);
    for (offset = 0; offset < 18; ++offset) {
        uint64_t original = entries[first + offset];
        for (bit = 0; bit < 64; ++bit) {
            int allowed = offset && offset < 17 && (bit == 5 || bit == 6);
            uint32_t previous = invalidations;
            entries[first + offset] = original ^ (1ULL << bit);
            memcpy(before, entries, sizeof(before));
            check_private(x86_task_stack_mapping_valid(owner, 1) == allowed);
            if (!allowed) {
                check_private(!x86_task_stack_revoke(owner));
                check_private(invalidations == previous && memcmp(before, entries, sizeof(before)) == 0);
            }
            entries[first + offset] = original;
        }
    }
    memset(data, 0xA5, sizeof(data));
    check_private(x86_task_stack_zero(owner));
    for (offset = 0; offset < sizeof(data); ++offset) check_private(!data[offset]);
    // Partial mapping rollback only clears matching owned entries.
    for (offset = 6; offset < 17; ++offset) entries[first + offset] = 0;
    check_private(x86_task_stack_revoke(owner));
    check_private(invalidations == 36 && x86_task_stack_mapping_valid(owner, 0));
    invalidations = 0;
}

// ====== code_permissions_private ======

static void code_permissions_private(void)
{
    uint32_t indices[4] = {0,4,0,0};
    for (uint32_t level = 0; level < 4; ++level) code[level][indices[level]] = 0x300000 | (level == 3 ? 1 : 3);
    read_levels = 0;
    check_private(x86_scheduler_code_valid(0x100000000) && read_levels == 4);
    for (uint32_t level = 0; level < 4; ++level) {
        uint64_t original = code[level][indices[level]];
        for (uint32_t bit = 0; bit < 64; ++bit) {
            int allowed = bit == 5 || bit == 6 || (bit >= 12 && bit < 48);
            code[level][indices[level]] = original ^ (1ULL << bit);
            read_levels = 0;
            check_private(x86_scheduler_code_valid(0x100000000) == allowed);
            if (!allowed) check_private(read_levels == level + 1); // Never dereference next parent after refusal.
            code[level][indices[level]] = original;
        }
    }
}

// ====== main ======

// ====== main ======

// ====== dispatcher_authority_private ======

static void dispatcher_authority_private(void)
{
    uint32_t offset, bit;
    pool.Live[0].Base = 0x300000; pool.Live[0].Pages = 16; pool.Live[0].Id = 7;
    pool.Live[0].Owner = pool.Self; pool.Pins[0] = 1;
    for (offset = 1; offset < 17; ++offset)
        entries[96 + offset] = (0x300000 + (offset - 1) * 4096ULL) | 3 | (1ULL << 63);
    check_private(x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536));
    check_private(!x86_scheduler_stack_authority(&contexts[6], X86_KERNEL_MAIN_BASE, 65536));
    for (offset = 0; offset < 18; ++offset) {
        uint64_t original = entries[96 + offset];
        for (bit = 0; bit < 64; ++bit) {
            int allowed = offset && offset < 17 && (bit == 5 || bit == 6);
            entries[96 + offset] = original ^ (1ULL << bit);
            check_private(x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536) == allowed);
            entries[96 + offset] = original;
        }
    }
    pool.Pins[0] = 0;
    check_private(!x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536));
    pool.Pins[0] = 2;
    check_private(!x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536));
    pool.Pins[0] = 1; pool.Live[0].Pages = 15;
    check_private(!x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536));
    pool.Live[0].Pages = 16; pool.Live[1] = pool.Live[0]; pool.Pins[1] = 1;
    check_private(!x86_scheduler_stack_authority(&contexts[7], X86_KERNEL_MAIN_BASE, 65536));
    memset(&pool.Live[1], 0, sizeof(pool.Live[1])); pool.Pins[1] = 0;
}

// ====== live_pages_private ======

static void live_pages_private(void)
{
    memset(owners, 0, sizeof(owners));
    memset(entries + 320, 0, 144 * sizeof(*entries));
    live_test = 1;
    check_private(x86_scheduler_live_stack_pages() == 0);
    for (uint32_t index = 0; index < 3; ++index) {
        X86_TASK_STACK *owner = &owners[index];
        owner->self = (uint64_t)(uintptr_t)owner; owner->root = 0x200000; owner->pool = &pool;
        owner->generation = 1; owner->index = index; owner->stage = X86_TASK_STACK_READY;
        owner->lease = (KERNEL_PHYSICAL_LEASE){0x100000 + index * 65536ULL, 16, index + 1, pool.Self};
        for (uint32_t offset = 1; offset < 17; ++offset)
            entries[320 + index * 18 + offset] = (owner->lease.Base + (offset - 1) * 4096ULL) | 3 | (1ULL << 63);
        check_private(x86_scheduler_live_stack_pages() == (index + 1) * 16);
    }
    entries[320] = 1; check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); entries[320] = 0;
    entries[320 + 3 * 18 + 1] = 1;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); entries[320 + 3 * 18 + 1] = 0;
    owners[3].generation = 1;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); owners[3].generation = 0;
    owners[1].lease.Id = 1;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); owners[1].lease.Id = 2;
    owners[1].stage = X86_TASK_STACK_MAPPING;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); owners[1].stage = X86_TASK_STACK_READY;
    owners[1].lease.Pages = 15;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); owners[1].lease.Pages = 16;
    owners[1].lease.Id = 4;
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX); owners[1].lease.Id = 2;
    live_test = 0;
}

// ====== main ======

int main(void)
{
    pool.Self = (uint64_t)(uintptr_t)&pool; pool.PhysicalBits = 48;
    entries[320] = 1;
    check_private(!x86_scheduler_bind(owners, contexts, &pool, 0x200000));
    entries[320] = 0;
    check_private(x86_scheduler_bind(owners, contexts, &pool, 0x200000));
    check_private(!x86_scheduler_bind(owners, contexts, &pool, 0x200000));
    for (uint32_t index = 0; index < 8; ++index) map_corruption_private(index);
    dispatcher_authority_private();
    code_permissions_private();
    live_pages_private();
    denied = 1;
    check_private(!x86_task_stack_map(&owners[0]));
    denied = 0; rsp = x86_task_stack_base(0) + 4096;
    check_private(!x86_task_stack_map(&owners[0]) && !x86_task_stack_revoke(&owners[0]));
    rsp = X86_KERNEL_MAIN_BASE + 4096;
    x86_scheduler_unbind();
    check_private(!x86_scheduler_native_gate());
    check_private(x86_scheduler_live_stack_pages() == UINT64_MAX);
    printf("PASS: %u native mapper/permission checks, whole-slot preflight and invalidation (CPU/PTE MOCK)\n",checks);
    return 0;
}
