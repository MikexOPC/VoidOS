#include "transition.h"
#include "content/ownership.h"

// ====== source_private ======

static int source_private(X86_AP_TRANSITION *owner, BOOT_U64 *page, int compare) {
    if (!owner->Io.Transfer(owner->Io.Context, owner->Root, page, 0)) return 0;
    for (BOOT_U32 index = 0; index < 512; ++index) {
        if (!xt_source_entry(page[index], owner->Pool->PhysicalBits) || (index < 256 && page[index]) ||
            (compare && (page[index] & ~0x60ULL) != (owner->Source[index] & ~0x60ULL))) return 0;
    }
    return (page[X86_LAYOUT_RECURSIVE_SLOT] & ~0x60ULL) == (owner->Root | (1ULL << 63) | 3);
}

// ====== entry_private ======

// Intel SDM 253668-078US 4.5.4/4.6: P=bit0, RW=bit1, U/S=bit2, XD=bit63.
// Low code RO/X, separate data RW/NX, supervisors only; PAT0 is validated WB.
static BOOT_U64 entry_private(const X86_AP_TRANSITION *owner, BOOT_U32 table, BOOT_U32 index) {
    return xt_entry(table, index, owner->Tables.Base, owner->Code->Lease.Base, owner->Data.Base,
        owner->Source[index], X86_LAYOUT_RECURSIVE_SLOT);
}

// ====== xt_build ======

int xt_build(X86_AP_TRANSITION *owner) {
    if (!owner || owner->Self != (BOOT_U64)owner || owner->State != 1 || xt_content_busy(owner) ||
        owner->Reserved || !owner->Pool || !owner->Io.Transfer || !owner->Io.Safe ||
        owner->DataPinned != 1 || owner->TablesPinned != 1 ||
        owner->Data.Pages != 1 || owner->Tables.Pages != 4 || !owner->Io.Safe(owner->Io.Context) ||
        !x86_ap_low_valid(owner->Code) || owner->Code->Pool != owner->Pool ||
        !x86_ap_low_is_held(owner->Code, owner) ||
        x86_ap_low_is_frozen(owner->Code, owner) ||
        !kernel_physical_lease_state(owner->Pool, &owner->Data, 1) ||
        !kernel_physical_lease_state(owner->Pool, &owner->Tables, 1) ||
        !xt_geometry(owner->Code->Lease.Base, owner->Data.Base, owner->Tables.Base) ||
        owner->Root < 4096 || (owner->Root & 4095) ||
        owner->Root >= (1ULL << owner->Pool->PhysicalBits) || (owner->Pat & 255) != 6) return 0;
    BOOT_U64 *page = owner->Scratch;
    if (!source_private(owner, owner->Source, 0)) return 0;
    for (BOOT_U32 index = 0; index < 512; ++index) page[index] = 0;
    if (!owner->Io.Transfer(owner->Io.Context, owner->Data.Base, page, 1)) return 0;
    for (BOOT_U32 table = 0; table < 4; ++table) {
        for (BOOT_U32 index = 0; index < 512; ++index) {
            page[index] = entry_private(owner, table, index);
            if (page[index] == UINT64_MAX) return 0;
        }
        if (!owner->Io.Transfer(owner->Io.Context, owner->Tables.Base + table * 4096ULL, page, 1)) return 0;
    }
    return source_private(owner, page, 1);
}

// ====== verify_tables_private ======

static int verify_tables_private(X86_AP_TRANSITION *owner, int live) {
    BOOT_U64 *page;
    if (!owner || owner->Self != (BOOT_U64)owner || (owner->State != 3 && owner->State != 4) || owner->Reserved ||
        !owner->Io.Safe || !owner->Io.Transfer || !owner->Pool ||
        !owner->DataPinned || !owner->TablesPinned || !x86_ap_low_valid(owner->Code) ||
        owner->Code->Pool != owner->Pool || owner->Data.Pages != 1 || owner->Tables.Pages != 4 ||
        !x86_ap_low_is_held(owner->Code, owner) ||
        x86_ap_low_is_frozen(owner->Code, owner) != (owner->State == 4) ||
        !xt_geometry(owner->Code->Lease.Base, owner->Data.Base, owner->Tables.Base) ||
        !kernel_physical_lease_state(owner->Pool, &owner->Data, 1) ||
        !kernel_physical_lease_state(owner->Pool, &owner->Tables, 1) ||
        !owner->Io.Safe(owner->Io.Context)) return 0;
    page = owner->Scratch;
    if (!source_private(owner, page, 1)) return 0;
    for (BOOT_U32 table = 0; table < 4; ++table) {
        if (!owner->Io.Transfer(owner->Io.Context, owner->Tables.Base + table * 4096ULL, page, 0)) return 0;
        for (BOOT_U32 index = 0; index < 512; ++index) {
            BOOT_U64 expected = entry_private(owner, table, index);
            // Intel SDM 253668-078US 4.8: A=5; D=6 only in final page entries.
            BOOT_U64 mask = live && (expected & 1) ? (table == 3 ? 0x60ULL : 0x20ULL) : 0;
            if ((page[index] ^ expected) & ~mask) return 0;
        }
    }
    return owner->Io.Safe(owner->Io.Context);
}

// ====== xt_verify_tables ======

int xt_verify_tables(X86_AP_TRANSITION *owner) {
    return verify_tables_private(owner, 0);
}

// ====== xt_verify_live_tables ======

int xt_verify_live_tables(X86_AP_TRANSITION *owner) {
    return owner && owner->State == 4 && verify_tables_private(owner, 1);
}

// ====== xt_verify ======

int xt_verify(X86_AP_TRANSITION *owner) {
    if (!xt_verify_tables(owner)) return 0;
    BOOT_U64 *page = owner->Scratch;
    if (!owner->Io.Transfer(owner->Io.Context, owner->Data.Base, page, 0)) return 0;
    for (BOOT_U32 index = 0; index < 512; ++index) if (page[index]) return 0;
    return owner->Io.Safe(owner->Io.Context);
}
