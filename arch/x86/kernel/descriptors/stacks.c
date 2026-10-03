#include "stacks.h"
#include "arch/x86/kernel/platform.h"
#define IST_NX (1ULL << 63)
#define IST_ADDRESS 0x000ffffffffff000ULL
#define IST_AD 0x60ULL

// ====== intervalli completi e allineati prima di leggere gli owner ===== //

static int metadata_range(const void *object, BOOT_U64 bytes, BOOT_U64 alignment)
{
    BOOT_U64 base = (BOOT_U64)object;
    BOOT_U64 end;
    if (!object || base < 4096 || (base & (alignment - 1)) ||
        base > X86_BOOT_LOW_ADDRESS_LIMIT - bytes) {
        return 0;
    }
    end = base + bytes;
    return (end <= X86_KERNEL_IST_GUARD || base >= X86_KERNEL_IST_GUARD + 16 * 4096) &&
        (end <= X86_KERNEL_MAIN_GUARD || base >= X86_KERNEL_MAIN_TOP + 4096);
}

// ====== genitori verificati prima di raggiungere una vista successiva ===== //

int x86_kernel_stack_branch(BOOT_U64 root, BOOT_U32 bits)
{
    BOOT_U32 level;
    const BOOT_U32 shifts[] = {39, 30, 21};
    volatile BOOT_U64 *entries;
    if (bits < 32 || bits > 52 || !root || (root & 4095) ||
        root >= (1ULL << bits) || root != x86_kernel_root()) {
        return 0;
    }
    entries = x86_stack_entries(0);
    if (!entries || (entries[X86_BOOT_RECURSIVE_SLOT] & ~IST_AD) != (root | 3 | IST_NX)) {
        return 0;
    }
    for (level = 0; level < 3; ++level) {
        BOOT_U64 entry, physical;
        entries = x86_stack_entries(level);
        if (!entries) {
            return 0;
        }
        entry = entries[(X86_KERNEL_IST_GUARD >> shifts[level]) & 511];
        physical = entry & IST_ADDRESS;
        if (!physical || physical >= (1ULL << bits) || (entry & ~IST_AD) != (physical | 3)) {
            return 0;
        }
    }
    return x86_stack_entries(3) != 0;
}

// ====== foglie e guardie confrontate senza adottare mapping estranei ===== //

static int leaves(const X86_KERNEL_STACKS *owner, int present, BOOT_U32 profile)
{
    BOOT_U32 slot;
    BOOT_U32 first = profile == 1 ? X86_KERNEL_IST_LEAF : X86_KERNEL_MAIN_LEAF;
    BOOT_U32 count = profile == 1 ? 16 : 18;
    volatile BOOT_U64 *entries = x86_stack_entries(3);
    if (!entries) {
        return 0;
    }
    for (slot = 0; slot < count; ++slot) {
        BOOT_U32 page = x86_stack_page(profile, slot);
        BOOT_U64 expected = present && page ? (owner->Lease.Base + (page - 1) * 4096) | 3 | IST_NX : 0;
        BOOT_U64 actual = entries[first + slot];
        if (expected ? (actual & ~IST_AD) != expected : actual != 0) {
            return 0;
        }
    }
    return 1;
}

// ====== invalidazioni del profilo dopo pubblicazione o revoca completa ===== //

static void invalidate(BOOT_U32 profile)
{
    BOOT_U32 slot;
    BOOT_U32 count = profile == 1 ? 16 : 18;
    BOOT_U64 guard = profile == 1 ? X86_KERNEL_IST_GUARD : X86_KERNEL_MAIN_GUARD;
    for (slot = 0; slot < count; ++slot) {
        if (x86_stack_page(profile, slot)) {
            x86_stack_invalidate(guard + slot * 4096);
        }
    }
}

// ====== azzeramento bounded prima della disponibilita per la TSS ===== //

static int clear_stacks(BOOT_U32 profile)
{
    BOOT_U32 stack, byte;
    BOOT_U32 begin = profile == 1 ? 1 : 4, end = profile == 1 ? 3 : 4;
    BOOT_U64 bytes = profile == 1 ? X86_KERNEL_IST_BYTES : X86_KERNEL_MAIN_BYTES;
    for (stack = begin; stack <= end; ++stack) {
        volatile unsigned char *data = x86_stack_bytes(stack);
        if (!data) return 0;
        for (byte = 0; byte < bytes; ++byte) data[byte] = 0;
        for (byte = 0; byte < bytes; ++byte) if (data[byte]) return 0;
    }
    return 1;
}

// ====== proprieta non spostabile e lease fisico esclusivo ===== //

int x86_kernel_stacks_validate(const X86_KERNEL_STACKS *owner)
{
    BOOT_U64 a = (BOOT_U64)owner, b;
    if (!metadata_range(owner, sizeof(*owner), _Alignof(X86_KERNEL_STACKS))) {
        return 0;
    }
    b = (BOOT_U64)owner->Pool;
    if (!metadata_range(owner->Pool, sizeof(*owner->Pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        (a < b + sizeof(*owner->Pool) && b < a + sizeof(*owner)) ||
        owner->Self != a || owner->Ready != 1 || owner->Committed > 1 ||
        owner->Profile < 1 || owner->Profile > 2 || owner->Reserved ||
        !kernel_physical_validate(owner->Pool) || owner->Lease.Pages != (owner->Profile == 1 ? 12U : 16U) ||
        !x86_stack_frame(owner->Profile, owner->Lease.Base, owner->Pool->PhysicalBits) ||
        !kernel_physical_is_pinned(owner->Pool, &owner->Lease)) {
        return 0;
    }
    return x86_kernel_stack_branch(owner->Root, owner->Pool->PhysicalBits) && leaves(owner, 1, owner->Profile);
}

// ====== azzeramento dei metadati solo dopo rilascio del prestito ===== //

static void forget(X86_KERNEL_STACKS *owner)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
}

// ====== rollback ammesso solo prima che la CPU possa usare gli stack ===== //

int x86_kernel_stacks_release(X86_KERNEL_STACKS *owner)
{
    BOOT_U32 slot;
    BOOT_U32 count, first;
    volatile BOOT_U64 *entries;
    if (!x86_kernel_stacks_validate(owner) || owner->Committed ||
        owner->Pool->Count == KERNEL_PHYSICAL_EXTENTS) {
        return 0;
    }
    entries = x86_stack_entries(3);
    count = owner->Profile == 1 ? 16 : 18;
    first = owner->Profile == 1 ? X86_KERNEL_IST_LEAF : X86_KERNEL_MAIN_LEAF;
    for (slot = 0; slot < count; ++slot) {
        if (x86_stack_page(owner->Profile, slot)) {
            entries[first + slot] = 0;
        }
    }
    invalidate(owner->Profile);
    if (!kernel_physical_pin(owner->Pool, &owner->Lease, 0) ||
        !kernel_physical_release(owner->Pool, &owner->Lease)) return 0;
    forget(owner);
    return 1;
}

// ====== input separati e owner vergine prima delle allocazioni ===== //

static int fresh(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool)
{
    BOOT_U32 byte;
    BOOT_U64 a = (BOOT_U64)owner, b = (BOOT_U64)pool;
    if (!metadata_range(owner, sizeof(*owner), _Alignof(X86_KERNEL_STACKS)) ||
        !metadata_range(pool, sizeof(*pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        (a < b + sizeof(*pool) && b < a + sizeof(*owner))) return 0;
    for (byte = 0; byte < sizeof(*owner); ++byte) {
        if (((const unsigned char *)owner)[byte]) return 0;
    }
    return kernel_physical_validate(pool);
}

// ====== blocco fisico contiguo con guardie esclusivamente virtuali ===== //

int x86_kernel_stacks_acquire_for(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U32 profile)
{
    BOOT_U32 slot;
    BOOT_U32 count = profile == 1 ? 16 : 18;
    BOOT_U32 first = profile == 1 ? X86_KERNEL_IST_LEAF : X86_KERNEL_MAIN_LEAF;
    BOOT_U64 pages = profile == 1 ? 12 : 16;
    volatile BOOT_U64 *entries;
    if (profile < 1 || profile > 2 || !fresh(owner, pool) ||
        !x86_kernel_stack_branch(root, pool->PhysicalBits) || !leaves(owner, 0, profile)) return 0;
    if (!kernel_physical_allocate(pool, pages, &owner->Lease)) return 0;
    if (!x86_stack_frame(profile, owner->Lease.Base, pool->PhysicalBits) || !kernel_physical_pin(pool, &owner->Lease, 1)) {
        if (!kernel_physical_release(pool, &owner->Lease)) return 0;
        return 0;
    }
    owner->Self = (BOOT_U64)owner;
    owner->Pool = pool;
    owner->Root = root;
    owner->Profile = profile;
    entries = x86_stack_entries(3);
    for (slot = 0; slot < count; ++slot) {
        BOOT_U32 page = x86_stack_page(profile, slot);
        if (page) entries[first + slot] = (owner->Lease.Base + (page - 1) * 4096) | 3 | IST_NX;
    }
    invalidate(profile);
    owner->Ready = 1;
    if (!clear_stacks(profile) || !x86_kernel_stacks_validate(owner)) {
        x86_kernel_stacks_release(owner);
        return 0;
    }
    return 1;
}

// ====== compatibilita del profilo IST senza duplicare il mapper ===== //

int x86_kernel_stacks_acquire(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    return x86_kernel_stacks_acquire_for(owner, pool, root, 1);
}

// ====== irreversibile per il bootstrap nessun FreePages dopo LTR ===== //

int x86_kernel_stacks_commit(X86_KERNEL_STACKS *owner)
{
    if (!x86_kernel_stacks_validate(owner) || owner->Committed) {
        return 0;
    }
    owner->Committed = 1;
    return 1;
}
