#include "mapping.h"
#include "arch/x86/kernel/platform.h"
#define NX (1ULL << 63)
#define ADDRESS 0x000ffffffffff000ULL
#define AD 0x60ULL

// ====== metadati canonici allineati distinti dalla loro arena dati ===== //

static int range(const void *object, BOOT_U64 bytes, BOOT_U64 alignment)
{
    BOOT_U64 base = (BOOT_U64)object;
    if (!object || base < 4096 || (base & (alignment - 1)) || base > X86_BOOT_LOW_ADDRESS_LIMIT - bytes) return 0;
    return base + bytes <= X86_REGION_GUARD || base >= X86_REGION_GUARD + 4 * 34 * 4096;
}

// ====== intervalli di metadati disgiunti dopo il controllo degli estremi ===== //

static int separate(const void *a, BOOT_U64 bytes, const void *b, BOOT_U64 other)
{
    return (BOOT_U64)a + bytes <= (BOOT_U64)b || (BOOT_U64)b + other <= (BOOT_U64)a;
}

// ====== validare ogni genitore prima di leggere il livello seguente ===== //

static int branch(BOOT_U64 root, BOOT_U32 bits)
{
    BOOT_U32 level;
    const BOOT_U32 shifts[] = {39, 30, 21};
    volatile BOOT_U64 *table;
    if (bits < 32 || bits > 52 || !root || (root & 4095) || root >= (1ULL << bits) || root != x86_kernel_root()) return 0;
    table = x86_region_entries(0);
    if (!table || (table[128] & ~AD) != (root | 3 | NX)) return 0;
    for (level = 0; level < 3; ++level) {
        BOOT_U64 entry, physical;
        table = x86_region_entries(level);
        if (!table) return 0;
        entry = table[(X86_REGION_GUARD >> shifts[level]) & 511]; physical = entry & ADDRESS;
        if (!physical || physical >= (1ULL << bits) || (entry & ~AD) != (physical | 3)) return 0;
    }
    return x86_region_entries(3) != 0;
}

// ====== foglie guardie e coda inutilizzata confrontate integralmente ===== //

static int leaves(const X86_REGION_MAPPER *owner)
{
    BOOT_U32 slot, page;
    volatile BOOT_U64 *table = x86_region_entries(3);
    for (slot = 0; slot < X86_REGION_SLOTS; ++slot) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Live[slot];
        for (page = 0; page < X86_REGION_STRIDE; ++page) {
            BOOT_U64 expected = lease->Id && page && page <= lease->Pages ? (lease->Base + (page - 1) * 4096) | 3 | NX : 0;
            BOOT_U64 actual = table[X86_REGION_LEAF + slot * X86_REGION_STRIDE + page];
            if (expected ? (actual & ~AD) != expected : actual != 0) return 0;
        }
    }
    return 1;
}

// ====== owner non spostabile e lease distinti prima di modificare le tabelle ===== //

int x86_region_validate(const X86_REGION_MAPPER *owner)
{
    BOOT_U32 slot, other;
    if (!range(owner, sizeof(*owner), _Alignof(X86_REGION_MAPPER)) || owner->Self != (BOOT_U64)owner ||
        owner->Ready != 1 || owner->Reserved || !range(owner->Pool, sizeof(*owner->Pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        !separate(owner, sizeof(*owner), owner->Pool, sizeof(*owner->Pool)) || !kernel_physical_validate(owner->Pool)) return 0;
    for (slot = 0; slot < X86_REGION_SLOTS; ++slot) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Live[slot];
        if (!lease->Id) { if (lease->Base || lease->Pages || lease->Owner || owner->Borrowers[slot]) return 0; continue; }
        if (owner->Borrowers[slot] && !range((const void *)owner->Borrowers[slot], 8, 8)) return 0;
        if (!x86_region_frame(lease->Base, lease->Pages, owner->Pool->PhysicalBits) ||
            !kernel_physical_is_pinned(owner->Pool, lease)) return 0;
        for (other = 0; other < slot; ++other) if (owner->Live[other].Id == lease->Id) return 0;
    }
    return branch(owner->Root, owner->Pool->PhysicalBits) && leaves(owner);
}

// ====== inizializzare solo oggetti vergini e PTE vuote senza adottare mapping estranei ===== //

int x86_region_initialize(X86_REGION_MAPPER *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    BOOT_U32 byte, entry;
    if (!range(owner, sizeof(*owner), _Alignof(X86_REGION_MAPPER)) ||
        !range(pool, sizeof(*pool), _Alignof(KERNEL_PHYSICAL_ALLOCATOR)) ||
        !separate(owner, sizeof(*owner), pool, sizeof(*pool)) || !kernel_physical_validate(pool)) return 0;
    for (byte = 0; byte < sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    if (!branch(root, pool->PhysicalBits)) return 0;
    for (entry = 0; entry < 4 * 34; ++entry) if (x86_region_entries(3)[128 + entry]) return 0;
    owner->Self = (BOOT_U64)owner; owner->Root = root; owner->Pool = pool; owner->Ready = 1;
    return 1;
}

// ====== output e handle non devono aliasare owner o metadati fisici ===== //

static int handle_range(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle)
{
    return range(handle, sizeof(*handle), _Alignof(X86_REGION_HANDLE)) &&
        separate(owner, sizeof(*owner), handle, sizeof(*handle)) && separate(owner->Pool, sizeof(*owner->Pool), handle, sizeof(*handle));
}

// ====== identita completa del prestito prima di esporre byte o revocare una PTE ===== //

static BOOT_U32 matching(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle)
{
    BOOT_U32 slot;
    if (!x86_region_validate(owner) || !handle_range(owner, handle) || handle->Owner != (BOOT_U64)owner || !handle->Id) return 0;
    slot = x86_region_slot(handle->Virtual);
    if (!slot || owner->Live[slot - 1].Id != handle->Id || owner->Live[slot - 1].Pages != handle->Pages) return 0;
    return slot;
}

// ====== invalidare tutto il batch prima di sbloccare o restituire frame ===== //

static void invalidate(BOOT_U32 slot, BOOT_U64 pages)
{
    BOOT_U64 page;
    for (page = 0; page < pages; ++page) x86_region_invalidate(x86_region_base(slot) + page * 4096);
}

// ====== capacita del rilascio fisico verificata prima della revoca irreversibile ===== //

static int remove_mapping(X86_REGION_MAPPER *owner, BOOT_U32 slot)
{
    BOOT_U32 entry;
    KERNEL_PHYSICAL_LEASE *lease = &owner->Live[slot];
    int fits = owner->Pool->Count < KERNEL_PHYSICAL_EXTENTS;
    for (entry = 0; !fits && entry < owner->Pool->Count; ++entry)
        fits = owner->Pool->Free[entry].Base + owner->Pool->Free[entry].Pages * 4096 == lease->Base ||
            lease->Base + lease->Pages * 4096 == owner->Pool->Free[entry].Base;
    if (!fits) return 0;
    for (entry = 0; entry < lease->Pages; ++entry) x86_region_entries(3)[128 + slot * 34 + 1 + entry] = 0;
    invalidate(slot, lease->Pages);
    return kernel_physical_pin(owner->Pool, lease, 0) && kernel_physical_release(owner->Pool, lease);
}

// ====== allineamento fisico pin e azzeramento prima di pubblicare l'handle ===== //

int x86_region_acquire(X86_REGION_MAPPER *owner, BOOT_U64 pages, BOOT_U64 alignment_pages, X86_REGION_HANDLE *output)
{
    BOOT_U32 slot, page;
    BOOT_U64 byte, bytes;
    volatile unsigned char *data;
    KERNEL_PHYSICAL_LEASE *lease;
    if (pages < 1 || pages > X86_REGION_PAGES || !x86_region_validate(owner) || !handle_range(owner, output) ||
        output->Owner || output->Id || output->Virtual || output->Pages) return 0;
    for (slot = 0; slot < X86_REGION_SLOTS; ++slot) if (!owner->Live[slot].Id) break;
    if (slot == X86_REGION_SLOTS) {
        return 0;
    }
    lease = &owner->Live[slot];
    if (!kernel_physical_allocate_aligned(owner->Pool, pages, alignment_pages, lease)) return 0;
    bytes = x86_region_frame(lease->Base, pages, owner->Pool->PhysicalBits);
    if (!bytes || !kernel_physical_pin(owner->Pool, lease, 1)) {
        kernel_physical_release(owner->Pool, lease);
        return 0;
    }
    for (page = 0; page < pages; ++page) x86_region_entries(3)[128 + slot * 34 + 1 + page] = (lease->Base + page * 4096) | 3 | NX;
    invalidate(slot, pages);
    data = x86_region_bytes(slot);
    if (!data) { remove_mapping(owner, slot); return 0; }
    for (byte = 0; byte < bytes; ++byte) data[byte] = 0;
    for (byte = 0; byte < bytes; ++byte) if (data[byte]) { remove_mapping(owner, slot); return 0; }
    // Un'invariante inattesa conserva il pin: mai liberare frame con mapping non verificati.
    if (!x86_region_validate(owner)) return 0;
    *output = (X86_REGION_HANDLE){(BOOT_U64)owner, lease->Id, x86_region_base(slot), pages};
    return 1;
}

// ====== scollegamento dei metadati soltanto dopo il rilascio di tutti i prestiti ===== //

int x86_region_detach(X86_REGION_MAPPER *owner)
{
    BOOT_U32 slot, byte;
    if (!x86_region_validate(owner)) {
        return 0;
    }
    for (slot = 0; slot < X86_REGION_SLOTS; ++slot) {
        if (owner->Live[slot].Id) {
            return 0;
        }
    }
    for (byte = 0; byte < sizeof(*owner); ++byte) {
        ((volatile unsigned char *)owner)[byte] = 0;
    }
    return 1;
}

// ====== prestito dei byte valido soltanto finche il chiamante mantiene l'handle vivo ===== //

volatile unsigned char *x86_region_data(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle)
{
    BOOT_U32 slot = matching(owner, handle);
    return slot ? x86_region_bytes(slot - 1) : 0;
}

// ====== rilascio esatto rende obsolete anche copie dell'handle dello stesso slot ===== //

int x86_region_release(X86_REGION_MAPPER *owner, X86_REGION_HANDLE *handle)
{
    BOOT_U32 slot = matching(owner, handle);
    if (!slot || owner->Borrowers[slot - 1] || !remove_mapping(owner, slot - 1)) return 0;
    *handle = (X86_REGION_HANDLE){0};
    return 1;
}

// ====== prestito esclusivo della durata del mapping per un componente persistente ===== //

int x86_region_bind(X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle, BOOT_U64 borrower)
{
    BOOT_U32 slot = matching(owner, handle);
    if (!slot || !range((const void *)borrower, 8, 8) || owner->Borrowers[slot - 1]) return 0;
    owner->Borrowers[slot - 1] = borrower;
    return 1;
}

// ====== solo la stessa identita puo terminare il prestito del mapping ===== //

int x86_region_unbind(X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle, BOOT_U64 borrower)
{
    BOOT_U32 slot = matching(owner, handle);
    if (!slot || !borrower || owner->Borrowers[slot - 1] != borrower) return 0;
    owner->Borrowers[slot - 1] = 0;
    return 1;
}

// ====== verifica immutabile del prestito prima di usare oggetti nel mapping ===== //

int x86_region_is_bound(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle, BOOT_U64 borrower)
{
    BOOT_U32 slot = matching(owner, handle);
    return slot && borrower && owner->Borrowers[slot - 1] == borrower;
}
