#include "main.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#define STACK_NX (1ULL << 63)
#define STACK_ADDRESS 0x000ffffffffff000ULL
#define STACK_AD 0x60ULL

// ====== intervalli canonici e metadati fuori dalle regioni degli stack ===== //

static int range(const void *object, BOOT_U64 bytes, BOOT_U64 alignment)
{
    BOOT_U64 base = (BOOT_U64)object;
    if (!object || base < 4096 || (base & (alignment - 1)) ||
        base > X86_BOOT_LOW_ADDRESS_LIMIT - bytes) {
        return 0;
    }
    return !os_address_ranges_overlap(base, bytes, X86_KERNEL_IST_GUARD, 65536) &&
        !os_address_ranges_overlap(base, bytes, X86_KERNEL_MAIN_GUARD, 18*4096) &&
        !os_address_ranges_overlap(base, bytes, X86_BOOT_STACK_GUARD, 18*4096);
}

// ====== mapping loader completo confrontato prima di qualsiasi revoca ===== //

static int loader_mapping(BOOT_U64 physical, BOOT_U32 bits)
{
    BOOT_U32 page;
    volatile BOOT_U64 *entries = x86_stack_entries(3);
    if (!entries || !x86_stack_frame(2, physical, bits) || entries[0] || entries[17]) {
        return 0;
    }
    for (page = 0; page < 16; ++page) {
        if ((entries[page + 1] & ~STACK_AD) != ((physical + page*4096) | 3 | STACK_NX)) return 0;
    }
    return 1;
}

// ====== nessun frame del vecchio stack nei free extent o lease gestiti ===== //

static int reserved_backing(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    BOOT_U32 index;
    for (index = 0; index < pool->Count; ++index) {
        if (os_address_ranges_overlap(pool->Free[index].Base, pool->Free[index].Pages*4096,
            physical, 65536)) {
            return 0;
        }
    }
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
        if (pool->Live[index].Id && os_address_ranges_overlap(pool->Live[index].Base,
            pool->Live[index].Pages*4096, physical, 65536)) {
            return 0;
        }
    }
    return 1;
}

// ====== owner vergine separato dal pool e dalla copia statica del contratto ===== //

static int arguments(X86_KERNEL_MAIN_STACK *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, const BOOT_START_INFO *info)
{
    BOOT_U32 byte;
    if (!range(owner, sizeof(*owner), _Alignof(X86_KERNEL_MAIN_STACK)) ||
        !range(pool, sizeof(*pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        !range(info, sizeof(*info), _Alignof(BOOT_START_INFO)) ||
        os_address_ranges_overlap((BOOT_U64)owner, sizeof(*owner), (BOOT_U64)pool, sizeof(*pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner, sizeof(*owner), (BOOT_U64)info, sizeof(*info)) ||
        os_address_ranges_overlap((BOOT_U64)info, sizeof(*info), (BOOT_U64)pool, sizeof(*pool))) return 0;
    for (byte = 0; byte < sizeof(*owner); ++byte) {
        if (((const unsigned char *)owner)[byte]) return 0;
    }
    return kernel_physical_validate(pool) && info->StackBase == X86_BOOT_STACK_VIRTUAL &&
        info->StackBytes == 65536 && x86_kernel_stack_branch(info->PageTableRoot, pool->PhysicalBits);
}

// ====== acquisizione del nuovo stack senza modificare quello attivo ===== //

int x86_kernel_main_prepare(X86_KERNEL_MAIN_STACK *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, const BOOT_START_INFO *info)
{
    BOOT_U64 physical, budget;
    if (!arguments(owner, pool, info)) {
        return 0;
    }
    physical = x86_stack_entries(3)[1] & STACK_ADDRESS;
    if (!loader_mapping(physical, pool->PhysicalBits) || !reserved_backing(pool, physical)) {
        return 0;
    }
    budget = pool->FreePages;
    if (!x86_kernel_stacks_acquire_for(&owner->Storage, pool, info->PageTableRoot, 2)) {
        return 0;
    }
    owner->LoaderPhysical = physical;
    owner->Budget = budget;
    owner->Phase = 1;
    return 1;
}

// ====== fase esplicita e budget prima che possano partire altre allocazioni ===== //

static int valid(const X86_KERNEL_MAIN_STACK *owner, BOOT_U32 phase)
{
    if (!range(owner, sizeof(*owner), _Alignof(X86_KERNEL_MAIN_STACK)) ||
        owner->Phase != phase || owner->Reserved || owner->Storage.Profile != 2 ||
        owner->Storage.Committed != (phase >= 2 ? 1U : 0U) ||
        !x86_kernel_stacks_validate(&owner->Storage) || owner->Budget < 16 ||
        owner->Storage.Pool->FreePages != owner->Budget - 16) {
        return 0;
    }
    return x86_stack_frame(2, owner->LoaderPhysical, owner->Storage.Pool->PhysicalBits) &&
        reserved_backing(owner->Storage.Pool, owner->LoaderPhysical);
}

// ====== rollback solo se la CPU non usa ancora il nuovo stack ===== //

int x86_kernel_main_release(X86_KERNEL_MAIN_STACK *owner)
{
    BOOT_U32 byte;
    BOOT_U64 rsp = x86_kernel_stack_pointer();
    // Anche un RSP disallineato dentro l'intervallo vieta di liberare le pagine.
    if (!valid(owner, 1) || (rsp >= X86_KERNEL_MAIN_BASE && rsp < X86_KERNEL_MAIN_TOP) ||
        !x86_kernel_stacks_release(&owner->Storage)) {
        return 0;
    }
    for (byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
    return 1;
}

// ====== pin permanente prima di pubblicare RSP0 o modificare RSP ===== //

int x86_kernel_main_commit(X86_KERNEL_MAIN_STACK *owner)
{
    BOOT_U64 rsp = x86_kernel_stack_pointer();
    if (!valid(owner, 1) || rsp < X86_BOOT_STACK_VIRTUAL || rsp >= X86_BOOT_STACK_TOP ||
        !loader_mapping(owner->LoaderPhysical, owner->Storage.Pool->PhysicalBits) ||
        !x86_kernel_stacks_commit(&owner->Storage)) {
        return 0;
    }
    owner->Phase = 2;
    return 1;
}

// ====== revoca locale del vecchio mapping solo dalla continuazione sul nuovo ===== //

int x86_kernel_main_retire(X86_KERNEL_MAIN_STACK *owner)
{
    BOOT_U32 page;
    volatile BOOT_U64 *entries;
    if (!valid(owner, 2) || !x86_main_pointer(x86_kernel_stack_pointer()) ||
        !loader_mapping(owner->LoaderPhysical, owner->Storage.Pool->PhysicalBits)) {
        return 0;
    }
    entries = x86_stack_entries(3);
    for (page = 1; page <= 16; ++page) entries[page] = 0;
    for (page = 0; page < 16; ++page) x86_stack_invalidate(X86_BOOT_STACK_VIRTUAL + page*4096);
    for (page = 0; page <= 17; ++page) if (entries[page]) return 0;
    owner->Phase = 3;
    // Le pagine loader sono riservate: nessun unpin o FreePages su quei frame.
    return 1;
}

// ====== campi numerici bounded solo dopo la verifica del nuovo mapping ===== //

static int field(const char *name, BOOT_U64 value)
{
    return kernel_text(name) && kernel_number(value) && kernel_text("\n");
}

// ====== successo osservabile solo con RSP proprio e vecchie foglie assenti ===== //

int x86_kernel_main_report(const X86_KERNEL_MAIN_STACK *owner)
{
    BOOT_U32 page;
    BOOT_U64 rsp = x86_kernel_stack_pointer();
    volatile BOOT_U64 *entries;
    if (!valid(owner, 3) || !x86_main_pointer(rsp)) {
        return 0;
    }
    entries = x86_stack_entries(3);
    for (page = 0; page <= 17; ++page) {
        if (entries[page]) return 0;
    }
    if (!field("KERNEL_MAIN_PHYSICAL: ", owner->Storage.Lease.Base) ||
        !field("KERNEL_MAIN_PTE: ", (BOOT_U64)(entries + X86_KERNEL_MAIN_LEAF)) ||
        !field("KERNEL_OLD_STACK_PTE: ", (BOOT_U64)entries) ||
        !field("KERNEL_OLD_STACK_PHYSICAL: ", owner->LoaderPhysical) ||
        !field("KERNEL_MAIN_RSP: ", rsp)) {
        return 0;
    }
    return kernel_log(0, "KERNEL_MAIN_STACK_OK");
}

// ====== confine senza ritorno nessun frame C viene copiato o riutilizzato ===== //

__declspec(noreturn) void x86_kernel_main_enter(X86_KERNEL_MAIN_STACK *owner, void (*next)(void))
{
    BOOT_U64 target = (BOOT_U64)next;
    // next e un ingresso statico RX del kernel, non un callback fornito dall'utente.
    if (!next || target < 0x100000000ULL || target >= 0x100100000ULL ||
        !x86_kernel_main_commit(owner) || !x86_kernel_segments_bind_main(&owner->Storage)) {
        kernel_panic("MAIN_STACK_ACTIVATION_FAILED");
    }
    x86_kernel_stack_switch(X86_KERNEL_MAIN_TOP, next);
}
