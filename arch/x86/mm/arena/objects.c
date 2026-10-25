#include "objects.h"
#include "arch/x86/configs/layout.h"

// ====== range canonico e allineato esterno all'arena dei mapping ===== //

static int range(const void *object, BOOT_U64 bytes, BOOT_U64 alignment)
{
    BOOT_U64 base = (BOOT_U64)object;
    // K1: kernel objects may sit in either canonical half (layout.h).
    if (!object || (base & (alignment - 1)) || !X86_LAYOUT_RANGE(base, bytes)) {
        return 0;
    }
    return base + bytes <= X86_REGION_GUARD || base >= X86_REGION_GUARD + 4 * 34 * 4096;
}

// ====== buffer distinti dopo il controllo esplicito degli estremi ===== //

static int separate(const void *a, BOOT_U64 bytes, const void *b, BOOT_U64 other)
{
    BOOT_U64 first = (BOOT_U64)a, second = (BOOT_U64)b;
    return first <= ~0ULL - bytes && second <= ~0ULL - other &&
        (first + bytes <= second || second + other <= first);
}

// ====== owner statico e mapper disgiunti dai metadati fisici ===== //

static int metadata(const X86_OBJECT_ARENA *owner, const X86_REGION_MAPPER *mapper)
{
    if (!range(owner, sizeof(*owner), _Alignof(X86_OBJECT_ARENA))) {
        return 0;
    }
    if (!range(mapper, sizeof(*mapper), _Alignof(X86_REGION_MAPPER))) {
        return 0;
    }
    if (!separate(owner, sizeof(*owner), mapper, sizeof(*mapper))) {
        return 0;
    }
    if (!x86_region_validate(mapper)) {
        return 0;
    }
    return separate(owner, sizeof(*owner), mapper->Pool, sizeof(*mapper->Pool));
}

// ====== vincoli del pool verificati prima di ogni accesso ai byte ===== //

int x86_object_arena_validate(const X86_OBJECT_ARENA *owner)
{
    volatile unsigned char *data;
    if (!range(owner, sizeof(*owner), _Alignof(X86_OBJECT_ARENA))) {
        return 0;
    }
    if (owner->Self != (BOOT_U64)owner || owner->Ready != 1 || owner->Reserved) {
        return 0;
    }
    if (!metadata(owner, owner->Mapper) ||
        !x86_region_is_bound(owner->Mapper, &owner->Region, (BOOT_U64)owner) ||
        owner->Region.Pages != 16 || !kernel_object_validate(&owner->Objects)) {
        return 0;
    }
    data = x86_region_data(owner->Mapper, &owner->Region);
    return data && owner->Objects.Arena == (unsigned char *)data && owner->Objects.Capacity == 65536;
}

// ====== prima apertura vergine o riapertura senza azzerare gli identificatori ===== //

static int available(const X86_OBJECT_ARENA *owner)
{
    BOOT_U32 byte;
    int fresh = 1;
    for (byte = 0; byte < sizeof(*owner); ++byte) {
        if (((const unsigned char *)owner)[byte]) {
            fresh = 0;
            break;
        }
    }
    if (fresh) {
        return 1;
    }
    if (owner->Self != (BOOT_U64)owner || owner->Mapper || owner->Ready || owner->Reserved ||
        owner->Region.Owner || owner->Region.Id || owner->Region.Virtual || owner->Region.Pages) {
        return 0;
    }
    if (owner->Objects.Self != (BOOT_U64)&owner->Objects ||
        !owner->Objects.NextId || owner->Objects.Ready ||
        owner->Objects.Arena || owner->Objects.Capacity || owner->Objects.Used || owner->Objects.Count) {
        return 0;
    }
    for (byte = 0; byte < KERNEL_OBJECT_RECORDS; ++byte) {
        const KERNEL_OBJECT_RECORD *record = &owner->Objects.Records[byte];
        if (record->Offset || record->Bytes || record->Alignment || record->Id) {
            return 0;
        }
    }
    return 1;
}

// ====== binding esclusivo prima di prestare il buffer al pool riutilizzato ===== //

int x86_object_arena_attach(X86_OBJECT_ARENA *owner, X86_REGION_MAPPER *mapper, const X86_REGION_HANDLE *region)
{
    volatile unsigned char *data;
    if (!metadata(owner, mapper) || !available(owner)) {
        return 0;
    }
    if (!range(region, sizeof(*region), _Alignof(X86_REGION_HANDLE))) {
        return 0;
    }
    if (!separate(owner, sizeof(*owner), region, sizeof(*region)) || region->Pages != 16) {
        return 0;
    }
    data = x86_region_data(mapper, region);
    if (!data || !separate(owner, sizeof(*owner), (const void *)data, 65536)) {
        return 0;
    }
    if (!x86_region_bind(mapper, region, (BOOT_U64)owner)) {
        return 0;
    }
    if (!kernel_object_initialize(&owner->Objects, (void *)data, 65536)) {
        x86_region_unbind(mapper, region, (BOOT_U64)owner);
        return 0;
    }
    owner->Self = (BOOT_U64)owner;
    owner->Mapper = mapper;
    owner->Region = *region;
    owner->Ready = 1;
    return x86_object_arena_validate(owner);
}

// ====== handle esterni a tutte le strutture che il wrapper potrebbe modificare ===== //

static int external(const X86_OBJECT_ARENA *owner, const KERNEL_OBJECT_HANDLE *handle)
{
    return range(handle, sizeof(*handle), _Alignof(KERNEL_OBJECT_HANDLE)) &&
        separate(owner, sizeof(*owner), handle, sizeof(*handle)) &&
        separate(owner->Mapper, sizeof(*owner->Mapper), handle, sizeof(*handle)) &&
        separate(owner->Mapper->Pool, sizeof(*owner->Mapper->Pool), handle, sizeof(*handle));
}

// ====== assegnazione con validatori Rust del pool dopo verifica del mapping vivo ===== //

int x86_object_arena_allocate(X86_OBJECT_ARENA *owner, BOOT_U64 bytes, BOOT_U64 alignment, KERNEL_OBJECT_HANDLE *output)
{
    unsigned char *data;
    BOOT_U64 byte;
    if (!x86_object_arena_validate(owner) || !external(owner, output)) {
        return 0;
    }
    if (!kernel_object_allocate(&owner->Objects, bytes, alignment, output)) {
        return 0;
    }
    data = kernel_object_data(&owner->Objects, output);
    if (!data) {
        return 0;
    }
    for (byte = 0; byte < bytes; ++byte) {
        if (data[byte]) {
            kernel_object_release(&owner->Objects, output);
            return 0;
        }
    }
    return 1;
}

// ====== accesso soltanto con identita dell'oggetto e mapping ancora corrispondenti ===== //

unsigned char *x86_object_arena_data(const X86_OBJECT_ARENA *owner, const KERNEL_OBJECT_HANDLE *handle)
{
    if (!x86_object_arena_validate(owner) || !external(owner, handle)) {
        return 0;
    }
    return kernel_object_data(&owner->Objects, handle);
}

// ====== rilascio azzera i byte dell'oggetto senza terminare il prestito dei frame ===== //

int x86_object_arena_release(X86_OBJECT_ARENA *owner, KERNEL_OBJECT_HANDLE *handle)
{
    if (!x86_object_arena_validate(owner) || !external(owner, handle)) {
        return 0;
    }
    return kernel_object_release(&owner->Objects, handle);
}

// ====== scollegare il pool prima di rendere nuovamente rilasciabile il mapping ===== //

int x86_object_arena_detach(X86_OBJECT_ARENA *owner)
{
    if (!x86_object_arena_validate(owner) || owner->Objects.Count || owner->Objects.Used) {
        return 0;
    }
    if (!kernel_object_detach(&owner->Objects)) {
        return 0;
    }
    if (!x86_region_unbind(owner->Mapper, &owner->Region, (BOOT_U64)owner)) {
        return 0;
    }
    owner->Mapper = 0;
    owner->Region = (X86_REGION_HANDLE){0};
    owner->Ready = 0;
    return 1;
}
