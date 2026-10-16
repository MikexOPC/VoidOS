#include "session.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "lib/memory/address_ranges.h"

// ====== x86_ps2_route_plan ======

int x86_ps2_route_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    BOOT_U32 physical_bits, X86_PS2_ROUTE *output)
{
    return x86_ps2_route_plan_source(topology,caps,physical_bits,1,output);
}

// ====== x86_ps2_route_plan_source ======

int x86_ps2_route_plan_source(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    BOOT_U32 physical_bits, BOOT_U32 source, X86_PS2_ROUTE *output)
{
    X86_PS2_ROUTE draft = {0,0,source,source == 1 ? 0x41U : 0x42U,0,0};
    BOOT_U32 index, found = 0;
    if ((source != 1 && source != 12) || !topology || !caps || !output || !kernel_madt_validate(topology,physical_bits) || caps->Ready != 1 ||
        caps->LapicId > 255 || caps->IoCount != topology->IoCount ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)topology,sizeof(*topology)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)caps,sizeof(*caps))) return 0;
    for (index = 0; index < topology->IsoCount; ++index) if (topology->Iso[index].Source == source) {
        BOOT_U32 polarity = topology->Iso[index].Flags & 3;
        BOOT_U32 trigger = (topology->Iso[index].Flags >> 2) & 3;
        if (polarity == 2 || trigger == 2 || topology->Iso[index].Flags & ~15U) return 0;
        draft.Gsi = topology->Iso[index].Gsi;
        draft.Bits |= (polarity == 3 ? 1U << 13 : 0) | (trigger == 3 ? 1U << 15 : 0);
    }
    for (index = 0; index < topology->NmiCount; ++index)
        if (topology->Nmi[index].Kind == 3 && topology->Nmi[index].Value == draft.Gsi) return 0;
    for (index = 0; index < 16; ++index) {
        BOOT_U32 gsi = index;
        if (index == source) continue;
        for (BOOT_U32 iso = 0; iso < topology->IsoCount; ++iso)
            if (topology->Iso[iso].Source == index) gsi = topology->Iso[iso].Gsi;
        if (gsi == draft.Gsi) return 0;
    }
    for (index = 0; index < caps->IoCount; ++index) {
        const X86_IO_APIC_CAPACITY *io = &caps->Io[index];
        if (io->Address != topology->Io[index].Address || io->Id != topology->Io[index].Id ||
            io->Base != topology->Io[index].GsiBase || io->Inputs != x86_apic_io_inputs(io->Version) ||
            !io->Inputs || io->End != x86_apic_gsi_end(io->Base,io->Inputs)) return 0;
        if (draft.Gsi >= io->Base && draft.Gsi < io->End) {
            draft.Io = index; draft.Pin = draft.Gsi - io->Base; ++found;
        }
    }
    if (found != 1) return 0;
    draft.Destination = caps->LapicId; *output = draft;
    return 1;
}

// ====== selected_read ======

static int selected_read(BOOT_U32 reg, BOOT_U32 *value)
{
    BOOT_U32 actual;
    /* Intel82093AA §3.1.1/3.1.2: selector offset0, 32-bit window offset10h.
       Verify selector before each window access; BSP/IF=0 exclusive borrow. */
    return x86_apic_control_write(0,reg) && x86_apic_control_read(0,&actual) &&
        (actual & 255) == reg && x86_apic_control_read(0x10,value);
}

// ====== selected_write ======

static int selected_write(BOOT_U32 reg, BOOT_U32 value)
{
    BOOT_U32 actual;
    return x86_apic_control_write(0,reg) && x86_apic_control_read(0,&actual) &&
        (actual & 255) == reg && x86_apic_control_write(0x10,value);
}

typedef struct { X86_PS2_SESSION *Owner; BOOT_U32 Masked; } REQUEST;

// ====== configure_route ======

static int configure_route(void *context)
{
    REQUEST *request = context;
    X86_PS2_SESSION *owner = request->Owner;
    const X86_IO_APIC_CAPACITY *io = &owner->Env.Capabilities->Io[owner->Route.Io];
    BOOT_U32 selector, actual, id, version, low = 0, high = 0, expected;
    BOOT_U32 reg = 0x10 + owner->Route.Pin * 2;
    int result, restored;
    /* Intel82093AA §3.2.4: low vector7:0, polarity13, trigger15, mask16;
       high destination31:24. QEMU hw/intc/ioapic.c implements this layout.
       Read-only delivery12/remote-IRR14 must be clear before replacement. */
    if (!x86_apic_control_read(0,&selector)) return 0;
    selector &= 255;
    result = selected_read(0,&id) && ((id >> 24) & 15) == io->Id &&
        selected_read(1,&version) && version == io->Version &&
        selected_read(reg,&low) && selected_read(reg+1,&high) && !(low & 0x5000);
    if (result && !owner->RouteOwned) {
        if (!(low & 0x10000) || request->Masked != 1) result = 0;
        else { owner->SavedLow = low; owner->SavedHigh = high; owner->RouteOwned = 1; }
    }
    expected = request->Masked == 2 ? owner->SavedLow : owner->Route.Bits | (request->Masked ? 0x10000 : 0);
    high = request->Masked == 2 ? owner->SavedHigh : owner->Route.Destination << 24;
    if (result) result = selected_write(reg,low | 0x10000) && selected_read(reg,&low) &&
        (low & 0x10000) && selected_write(reg+1,high) && selected_write(reg,expected) &&
        selected_read(reg,&low) && (low & ~0x5000U) == expected &&
        selected_read(reg+1,&actual) && actual == high;
    restored = x86_apic_control_write(0,selector) && x86_apic_control_read(0,&actual) && (actual & 255) == selector;
    if (result && restored && request->Masked == 2) owner->RouteOwned = 0;
    return result && restored;
}

// ====== x86_ps2_route_apply ======

int x86_ps2_route_apply(X86_PS2_SESSION *owner, BOOT_U32 masked)
{
    REQUEST request = {owner,masked};
    const X86_IO_APIC_CAPACITY *io;
    if (!owner || !owner->Env.Capabilities || !owner->Env.Profile || masked > 2 ||
        owner->Env.Capabilities->Ready != 1 || owner->Env.Capabilities->IoCount > 16 ||
        owner->Route.Io >= owner->Env.Capabilities->IoCount ||
        owner->Route.Reserved || ((owner->Route.Bits & ~0xa000U) != 0x41 &&
            (owner->Route.Bits & ~0xa000U) != 0x42) ||
        owner->Route.Destination != owner->Env.Profile->InitialId || owner->Route.Destination > 255) return 0;
    io = &owner->Env.Capabilities->Io[owner->Route.Io];
    if (owner->Route.Pin >= io->Inputs || owner->Route.Pin >= 120 ||
        owner->Route.Gsi != (BOOT_U64)io->Base + owner->Route.Pin || io->Id > 15 ||
        (masked == 2 && !owner->Quiet)) return 0;
    return x86_apic_borrow(owner->Env.Window,io->Address,owner->Env.Profile->CacheBits,1,configure_route,&request);
}

// ====== x86_ps2_mouse_route_apply ======

int x86_ps2_mouse_route_apply(X86_PS2_SESSION *owner, BOOT_U32 masked)
{
    X86_PS2_SESSION draft = {0};
    int result;
    if (!owner || (owner->Mouse.Route.Bits & ~0xa000U) != 0x42) return 0;
    draft.Env = owner->Env; draft.Quiet = owner->Quiet;
    draft.Route = owner->Mouse.Route;
    draft.SavedLow = owner->Mouse.SavedLow; draft.SavedHigh = owner->Mouse.SavedHigh;
    draft.RouteOwned = owner->Mouse.RouteOwned;
    result = x86_ps2_route_apply(&draft,masked);
    owner->Mouse.SavedLow = draft.SavedLow; owner->Mouse.SavedHigh = draft.SavedHigh;
    owner->Mouse.RouteOwned = draft.RouteOwned;
    return result;
}
