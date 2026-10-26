#include "native.h"
#include "arch/x86/configs/layout.h"

#define TASK_XD (UINT64_C(1) << 63)
#define TASK_ADDRESS UINT64_C(0x000FFFFFFFFFF000)
#define TASK_AD UINT64_C(0x60)
static X86_TASK_STACK *bound_owners;
static X86_TASK_CONTEXT *bound_contexts;
static KERNEL_PHYSICAL_ALLOCATOR *bound_pool;
static uint64_t bound_root;
static uint32_t bound_bsp;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 5
static uint32_t zero_fault_used;

// ====== x86_scheduler_zero_fault_used ======

uint32_t x86_scheduler_zero_fault_used(void) { return zero_fault_used; }
#endif

// ====== x86_scheduler_native_gate ======

int x86_scheduler_native_gate(void)
{
    X86_TASK_CPU cpu = {0};
    return bound_pool && x86_scheduler_cpu_capture(&cpu) &&
        x86_scheduler_cpu_valid(&cpu, bound_root, bound_bsp) &&
        !(cpu.cr4 & ((UINT64_C(1) << 7) | (UINT64_C(1) << 17))) &&
        x86_scheduler_cpu_permissions() == 1 && x86_exception_environment() &&
        x86_kernel_stack_branch(bound_root, bound_pool->PhysicalBits);
}

// ====== metadata_private ======

static int metadata_private(const void *object, uint64_t bytes, uint64_t alignment)
{
    uint64_t address = (uint64_t)(uintptr_t)object;
    uint64_t end = X86_TASK_STACK_GUARD + 8 * 18 * 4096ULL;
    // K1: kernel objects may sit in either canonical half (layout.h).
    return !(address & (alignment - 1)) && X86_LAYOUT_RANGE(address, bytes) &&
        (address + bytes <= X86_TASK_STACK_GUARD || address >= end);
}

// ====== separate_private ======

static int separate_private(const void *a, uint64_t bytes, const void *b, uint64_t other)
{
    uint64_t first = (uint64_t)(uintptr_t)a, second = (uint64_t)(uintptr_t)b;
    return first + bytes <= second || second + other <= first;
}

// ====== x86_scheduler_bind ======

int x86_scheduler_bind(X86_TASK_STACK *owners, X86_TASK_CONTEXT *contexts,
                      KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root)
{
    X86_TASK_CPU cpu = {0};
    if (bound_pool || !metadata_private(owners, 8 * sizeof(*owners), _Alignof(X86_TASK_STACK)) ||
        !metadata_private(contexts, 8 * sizeof(*contexts), 16) ||
        !metadata_private(pool, sizeof(*pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        !separate_private(owners, 8 * sizeof(*owners), contexts, 8 * sizeof(*contexts)) ||
        !separate_private(pool, sizeof(*pool), owners, 8 * sizeof(*owners)) ||
        !separate_private(pool, sizeof(*pool), contexts, 8 * sizeof(*contexts)) ||
        !kernel_physical_validate(pool) || !x86_scheduler_cpu_capture(&cpu) ||
        !x86_scheduler_cpu_valid(&cpu, root, cpu.apic_id) ||
        !x86_main_pointer(x86_kernel_stack_pointer())) return 0;
    bound_owners = owners; bound_contexts = contexts; bound_pool = pool;
    bound_root = root; bound_bsp = cpu.apic_id;
    if (!x86_scheduler_native_gate()) { x86_scheduler_unbind(); return 0; }
    for (uint32_t index = 320; index < 464; ++index)
        if (x86_stack_entries(3)[index]) { x86_scheduler_unbind(); return 0; }
    return 1;
}

// ====== x86_scheduler_unbind ======

// Only after dispatcher verified all owners empty and all slots absent.
void x86_scheduler_unbind(void)
{
    bound_owners = 0; bound_contexts = 0; bound_pool = 0; bound_root = 0; bound_bsp = 0;
}

// ====== x86_task_stack_dispatcher ======

int x86_task_stack_dispatcher(const X86_TASK_STACK *owner)
{
    return owner && bound_owners && owner->index < 8 && owner == &bound_owners[owner->index] &&
        owner->pool == bound_pool && owner->root == bound_root &&
        x86_main_pointer(x86_kernel_stack_pointer()) && x86_scheduler_native_gate();
}

// ====== slot_private ======

static int slot_private(const X86_TASK_STACK *owner, uint32_t mode)
{
    volatile BOOT_U64 *entries;
    uint32_t offset, first;
    if (!owner || !bound_owners || owner->index >= 8 || owner != &bound_owners[owner->index] ||
        owner->self != (uint64_t)(uintptr_t)owner || owner->pool != bound_pool ||
        owner->root != bound_root || mode > 2 || !x86_scheduler_native_gate()) return 0;
    entries = x86_stack_entries(3);
    first = X86_TASK_STACK_LEAF + owner->index * X86_TASK_STACK_STRIDE;
    for (offset = 0; offset < 18; ++offset) {
        uint64_t actual = entries[first + offset], expected = 0;
        if (offset && offset < 17 && mode) expected = (owner->lease.Base + (offset - 1) * 4096ULL) | 3 | TASK_XD;
        // Mode2 permits absent data PTEs during rollback; never foreign entries.
        if (mode == 2 && !actual) continue;
        if (expected ? (actual & ~TASK_AD) != expected : actual != 0) return 0;
    }
    return 1;
}

// ====== x86_task_stack_mapping_valid ======

int x86_task_stack_mapping_valid(const X86_TASK_STACK *owner, uint32_t mode)
{
    return mode <= 1 && slot_private(owner, mode);
}

// ====== invalidate_private ======

// Intel SDM093 Vol3A §5.10.4: invalidate before reuse; BSP/PCID=PGE=0 profile.
static void invalidate_private(const X86_TASK_STACK *owner)
{
    uint64_t guard = x86_task_stack_base(owner->index) - 4096;
    for (uint32_t offset = 0; offset < 18; ++offset) x86_invalidate_page(guard + offset * 4096ULL);
}

// ====== x86_task_stack_map ======

// Intel SDM093 Vol3A §5.5.4: P0/RW1/US2/XD63; WB PAT index0 inherited profile.
int x86_task_stack_map(const X86_TASK_STACK *owner)
{
    uint32_t first;
    volatile BOOT_U64 *entries;
    if (!x86_task_stack_dispatcher(owner) || !slot_private(owner, 0) ||
        kernel_physical_is_pinned(owner->pool, &owner->lease) != 1) return 0;
    entries = x86_stack_entries(3); first = X86_TASK_STACK_LEAF + owner->index * 18;
    for (uint32_t page = 0; page < 16; ++page)
        entries[first + page + 1] = (owner->lease.Base + page * 4096ULL) | 3 | TASK_XD;
    invalidate_private(owner);
    return slot_private(owner, 1);
}

// ====== x86_task_stack_revoke ======

int x86_task_stack_revoke(const X86_TASK_STACK *owner)
{
    volatile BOOT_U64 *entries;
    uint32_t first;
    if (!x86_task_stack_dispatcher(owner) || !slot_private(owner, 2)) return 0;
    entries = x86_stack_entries(3); first = X86_TASK_STACK_LEAF + owner->index * 18;
    for (uint32_t page = 0; page < 16; ++page) entries[first + page + 1] = 0;
    invalidate_private(owner);
    return slot_private(owner, 0);
}

// ====== x86_task_stack_zero ======

int x86_task_stack_zero(const X86_TASK_STACK *owner)
{
    volatile unsigned char *bytes;
    if (!x86_task_stack_dispatcher(owner) || !slot_private(owner, 1)) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 5
    if (!zero_fault_used) { zero_fault_used = 1; return 0; }
#endif
    bytes = x86_scheduler_native_bytes(owner->index);
    if (!bytes) return 0;
    for (uint32_t offset = 0; offset < 65536; ++offset) bytes[offset] = 0;
    for (uint32_t offset = 0; offset < 65536; ++offset) if (bytes[offset]) return 0;
    return slot_private(owner, 1);
}

// ====== x86_scheduler_stack_authority ======

int x86_scheduler_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    if (!bound_contexts || bytes != 65536) return 0;
    if (base == X86_KERNEL_MAIN_BASE && x86_scheduler_dispatcher_context_is(context) == 1) {
        volatile BOOT_U64 *entries;
        uint64_t physical;
        uint32_t matches = 0;
        if (!x86_scheduler_native_gate() || !kernel_physical_validate(bound_pool)) return 0;
        entries = x86_stack_entries(3);
        physical = entries[X86_KERNEL_MAIN_LEAF + 1] & TASK_ADDRESS;
        if (!physical || physical > (UINT64_C(1) << bound_pool->PhysicalBits) - 65536) return 0;
        for (uint32_t index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
            const KERNEL_PHYSICAL_LEASE *lease = &bound_pool->Live[index];
            if (lease->Base == physical && lease->Pages == 16 && lease->Id &&
                lease->Owner == bound_pool->Self && bound_pool->Pins[index] == 1) ++matches;
        }
        if (matches != 1) return 0;
        for (uint32_t offset = 0; offset < 18; ++offset) {
            uint64_t actual = entries[X86_KERNEL_MAIN_LEAF + offset];
            uint64_t expected = offset && offset < 17 ? (physical + (offset - 1) * 4096ULL) | 3 | TASK_XD : 0;
            if (expected ? (actual & ~TASK_AD) != expected : actual != 0) return 0;
        }
        return 1;
    }
    for (uint32_t index = 0; index < 8; ++index)
        if (context == &bound_contexts[index] && base == x86_task_stack_base(index))
            return x86_task_stack_validate(&bound_owners[index]);
    return 0;
}

// ====== x86_scheduler_live_stack_pages ======

uint64_t x86_scheduler_live_stack_pages(void)
{
    uint64_t pages = 0;
    volatile BOOT_U64 *entries;
    if (!bound_owners || !x86_scheduler_native_gate() ||
        !kernel_physical_validate(bound_pool) || !(entries = x86_stack_entries(3))) return UINT64_MAX;
    for (uint32_t index = 0; index < 8; ++index) {
        const X86_TASK_STACK *owner = &bound_owners[index];
        if (!owner->self) {
            for (uint32_t byte = 0; byte < sizeof(*owner); ++byte)
                if (((const unsigned char *)owner)[byte]) return UINT64_MAX;
            for (uint32_t offset = 0; offset < 18; ++offset)
                if (entries[320 + index * 18 + offset]) return UINT64_MAX;
            continue;
        }
        if (owner->self != (uint64_t)(uintptr_t)owner || owner->index != index ||
            owner->pool != bound_pool || owner->root != bound_root || !owner->generation ||
            owner->stage != X86_TASK_STACK_READY || owner->lease.Pages != 16 ||
            !slot_private(owner, 1) || kernel_physical_is_pinned(bound_pool, &owner->lease) != 1)
            return UINT64_MAX;
        for (uint32_t other = 0; other < index; ++other) {
            const X86_TASK_STACK *previous = &bound_owners[other];
            if (previous->self && (previous->lease.Id == owner->lease.Id ||
                !(owner->lease.Base + 65536 <= previous->lease.Base ||
                    previous->lease.Base + 65536 <= owner->lease.Base))) return UINT64_MAX;
        }
        pages += 16;
    }
    return pages;
}

// ====== x86_scheduler_live_stack_pages_for ======

uint64_t x86_scheduler_live_stack_pages_for(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root)
{
    if (!pool || pool != bound_pool || root != bound_root) return UINT64_MAX;
    return x86_scheduler_live_stack_pages();
}

// ====== x86_scheduler_code_valid ======

// Intel SDM093 §5.5.4: recursive access after parent checks, 4KiB supervisor RX only.
int x86_scheduler_code_valid(uint64_t target)
{
    uint64_t indices[4];
    uint64_t mask;
    if (!x86_scheduler_native_gate() || !X86_LAYOUT_RANGE(target, 1)) return 0;
    indices[0] = (target >> 39) & 511; indices[1] = (target >> 30) & 511;
    indices[2] = (target >> 21) & 511; indices[3] = (target >> 12) & 511;
    mask = ((UINT64_C(1) << bound_pool->PhysicalBits) - 1) & TASK_ADDRESS;
    for (uint32_t level = 0; level < 4; ++level) {
        volatile uint64_t *entries = x86_scheduler_code_entries(level, target);
        uint64_t entry;
        if (!entries) return 0;
        entry = entries[indices[level]];
        uint64_t frame = entry & mask, required = level == 3 ? 1 : 3;
        if (!frame || (entry & ~TASK_AD) != (frame | required)) return 0;
    }
    return 1;
}
