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
    return end <= X86_KERNEL_IST_GUARD || base >= X86_KERNEL_IST_GUARD + 16 * 4096;
}

// ====== genitori verificati prima di raggiungere una vista successiva ===== //

static int parents(BOOT_U64 root, BOOT_U32 bits)
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

static int leaves(const X86_KERNEL_STACKS *owner, int present)
{
    BOOT_U32 slot;
    volatile BOOT_U64 *entries = x86_stack_entries(3);
    if (!entries) {
        return 0;
    }
    for (slot = 0; slot < 16; ++slot) {
        BOOT_U32 page = x86_ist_page(slot);
        BOOT_U64 expected = present && page ? (owner->Lease.Base + (page - 1) * 4096) | 3 | IST_NX : 0;
        BOOT_U64 actual = entries[X86_KERNEL_IST_LEAF + slot];
        if (expected ? (actual & ~IST_AD) != expected : actual != 0) {
            return 0;
        }
    }
    return 1;
}

// ====== dodici invalidazioni dopo pubblicazione o revoca completa ===== //

static void invalidate(void)
{
    BOOT_U32 slot;
    for (slot = 0; slot < 16; ++slot) {
        if (x86_ist_page(slot)) {
            x86_stack_invalidate(X86_KERNEL_IST_GUARD + slot * 4096);
        }
    }
}

// ====== azzeramento bounded prima della disponibilita per la TSS ===== //

static int clear_stacks(void)
{
    BOOT_U32 stack, byte;
    for (stack = 1; stack <= 3; ++stack) {
        volatile unsigned char *data = x86_stack_bytes(stack);
        if (!data) return 0;
        for (byte = 0; byte < X86_KERNEL_IST_BYTES; ++byte) data[byte] = 0;
        for (byte = 0; byte < X86_KERNEL_IST_BYTES; ++byte) if (data[byte]) return 0;
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
        !kernel_physical_validate(owner->Pool) || owner->Lease.Pages != 12 ||
        !x86_ist_frame(owner->Lease.Base, owner->Pool->PhysicalBits) ||
        !kernel_physical_is_pinned(owner->Pool, &owner->Lease)) {
        return 0;
    }
    return parents(owner->Root, owner->Pool->PhysicalBits) && leaves(owner, 1);
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
    volatile BOOT_U64 *entries;
    if (!x86_kernel_stacks_validate(owner) || owner->Committed ||
        owner->Pool->Count == KERNEL_PHYSICAL_EXTENTS) {
        return 0;
    }
    entries = x86_stack_entries(3);
    for (slot = 0; slot < 16; ++slot) {
        if (x86_ist_page(slot)) {
            entries[X86_KERNEL_IST_LEAF + slot] = 0;
        }
    }
    invalidate();
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

int x86_kernel_stacks_acquire(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    BOOT_U32 slot;
    volatile BOOT_U64 *entries;
    if (!fresh(owner, pool) || !parents(root, pool->PhysicalBits) || !leaves(owner, 0)) return 0;
    if (!kernel_physical_allocate(pool, 12, &owner->Lease)) return 0;
    if (!x86_ist_frame(owner->Lease.Base, pool->PhysicalBits) || !kernel_physical_pin(pool, &owner->Lease, 1)) {
        if (!kernel_physical_release(pool, &owner->Lease)) return 0;
        return 0;
    }
    owner->Self = (BOOT_U64)owner;
    owner->Pool = pool;
    owner->Root = root;
    entries = x86_stack_entries(3);
    for (slot = 0; slot < 16; ++slot) {
        BOOT_U32 page = x86_ist_page(slot);
        if (page) entries[X86_KERNEL_IST_LEAF + slot] = (owner->Lease.Base + (page - 1) * 4096) | 3 | IST_NX;
    }
    invalidate();
    owner->Ready = 1;
    if (!clear_stacks() || !x86_kernel_stacks_validate(owner)) {
        x86_kernel_stacks_release(owner);
        return 0;
    }
    return 1;
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
