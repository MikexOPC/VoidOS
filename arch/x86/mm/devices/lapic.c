#include "lapic.h"
#ifdef KERNEL_PANIC_RUNTIME
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#endif
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/apic/lvt.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "lib/memory/address_ranges.h"
#include "kernel/diagnostics/log.h"
#define NX (1ULL << 63)
#define AD 0x60ULL
#define ADDRESS 0x000ffffffffff000ULL

// ====== owner kernel allineato fuori dai mapping dati e ricorsivi ===== //

static int range(const void *pointer, BOOT_U64 bytes)
{
    BOOT_U64 address = (BOOT_U64)pointer;
    return pointer && !(address & 7) && address <= ~0ULL-bytes &&
        !os_address_ranges_overlap(address,bytes,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,bytes,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,bytes,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES);
}

// ====== nessun alias tra owner scrivibile e input di preflight ===== //

static int separate(const X86_LAPIC_MAPPING *owner, const void *input, BOOT_U64 bytes)
{
    return !os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)input,bytes);
}

// ====== radice corrente e genitori WB supervisor prima della foglia dedicata ===== //

static int branch(BOOT_U64 root, BOOT_U32 bits)
{
    BOOT_U32 level;
    const BOOT_U32 shifts[3] = {39,30,21};
    volatile BOOT_U64 *entries;
    if (!x86_lapic_pte(root,0,bits) || root != x86_kernel_root()) return 0;
    entries = x86_window_entries(0);
    if (!entries || (entries[X86_BOOT_RECURSIVE_SLOT] & ~AD) != (root | 3 | NX)) return 0;
    for (level = 0; level < 3; ++level) {
        BOOT_U64 entry, physical;
        entries = x86_window_entries(level);
        if (!entries) return 0;
        entry = entries[(X86_LAPIC_VIRTUAL >> shifts[level]) & 511];
        physical = entry & ADDRESS;
        if (!x86_lapic_pte(physical,0,bits) || (entry & ~AD) != (physical | 3)) return 0;
    }
    entries = x86_window_entries(3);
    return entries && !entries[X86_LAPIC_LEAF-1] && !entries[X86_LAPIC_LEAF+1];
}

// ====== owner non trasferibile e cache coerente prima degli accessi MMIO ===== //

int x86_lapic_validate(const X86_LAPIC_MAPPING *owner)
{
    BOOT_U64 expected;
    if (!x86_exception_environment() || !range(owner,sizeof(*owner)) ||
        owner->Self != (BOOT_U64)owner || owner->Ready != 1 ||
        !branch(owner->Root,owner->PhysicalBits)) return 0;
    expected = x86_lapic_pte(owner->Physical,owner->Cache,owner->PhysicalBits);
    if (!expected) return 0;
    return (x86_window_entries(3)[X86_LAPIC_LEAF] & ~AD) == expected;
}

// ====== identita xAPIC e UC forte confermate dal PAT corrente ===== //

int x86_lapic_authority(const X86_LAPIC_MAPPING *owner)
{
    X86_APIC_RAW raw = {0};
    BOOT_U32 slot;
    if (!x86_lapic_validate(owner) || !x86_apic_cache_state() || !x86_apic_read_profile(&raw) ||
        raw.MaxLeaf < 1 || raw.Reserved || raw.PhysicalBits != owner->PhysicalBits ||
        (raw.Rflags & 0x202) != 2 ||
        x86_apic_mode(raw.ApicBase,raw.Leaf1Ecx,raw.Leaf1Edx,owner->PhysicalBits) != 1 ||
        (raw.ApicBase & ADDRESS) != owner->Physical || !x86_apic_uc_slot(raw.Pat)) return 0;
    slot = (BOOT_U32)((owner->Cache & 8) >> 3) +
        (BOOT_U32)((owner->Cache & 16) >> 3) + (BOOT_U32)((owner->Cache & 128) >> 5);
    return ((raw.Pat >> (slot*8)) & 255) == 0;
}

// ====== stato vergine senza adozione di pagine o owner estranei ===== //

static int virgin(const X86_LAPIC_MAPPING *owner)
{
    BOOT_U32 byte;
    if (!range(owner,sizeof(*owner))) return 0;
    for (byte = 0; byte < sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte]) return 0;
    return 1;
}

// ====== output distinto da metadati e copia memory map persistente ===== //

static int output_valid(const KERNEL_BOOT_CONTEXT *context, const X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, const X86_LAPIC_MAPPING *owner)
{
    return context && window && window->Pool && inventory && topology && profile &&
        virgin(owner) && separate(owner,context,sizeof(*context)) &&
        separate(owner,window,sizeof(*window)) && separate(owner,window->Pool,sizeof(*window->Pool)) &&
        separate(owner,inventory,sizeof(*inventory)) && separate(owner,topology,sizeof(*topology)) &&
        separate(owner,profile,sizeof(*profile)) &&
        context->Info.MemoryMapCapacity <= 1048576 &&
        separate(owner,context->MemoryMap,context->Info.MemoryMapCapacity);
}

// ====== pubblicazione singola UC RW NX con rollback del solo slot acquisito ===== //

static void clear_owner(X86_LAPIC_MAPPING *owner)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
}

// ====== bind dopo preflight RAM cache e riserve firmware ===== //

int x86_lapic_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, X86_LAPIC_MAPPING *owner)
{
    volatile BOOT_U64 *entries;
    BOOT_U64 budget, expected;
    if (!x86_exception_environment() || !output_valid(context,window,inventory,topology,profile,owner) ||
        !x86_apic_mapping_ready(context,window,inventory,topology,profile) ||
        !branch(window->Root,window->Pool->PhysicalBits)
    ) return 0;
    entries = x86_window_entries(3);
    if (entries[X86_LAPIC_LEAF]) return 0;
    if (!kernel_physical_budget_read(window->Pool, &budget)) return 0;
    expected = x86_lapic_pte(profile->Lapic,profile->CacheBits,window->Pool->PhysicalBits);
    if (!expected) return 0;
    entries[X86_LAPIC_LEAF] = expected;
    x86_invalidate_page(X86_LAPIC_VIRTUAL);
    if ((entries[X86_LAPIC_LEAF] & ~AD) != expected || !kernel_physical_budget_matches(window->Pool, budget)) {
        // Lo slot era vuoto e questa CPU lo ha appena pubblicato con IF zero.
        entries[X86_LAPIC_LEAF] = 0;
        x86_invalidate_page(X86_LAPIC_VIRTUAL);
        return 0;
    }
    owner->Self = (BOOT_U64)owner; owner->Root = window->Root;
    owner->Physical = profile->Lapic; owner->Cache = profile->CacheBits;
    owner->PhysicalBits = window->Pool->PhysicalBits; owner->Ready = 1;
    if (x86_lapic_validate(owner)) return 1;
    entries[X86_LAPIC_LEAF] = 0;
    x86_invalidate_page(X86_LAPIC_VIRTUAL);
    clear_owner(owner);
    return 0;
}

// ====== revoca soltanto a sorgente chiusa e senza liberare RAM del dispositivo ===== //

int x86_lapic_unbind(X86_LAPIC_MAPPING *owner) {
#ifdef KERNEL_PANIC_RUNTIME
    if (xp_panic_lapic_held(owner)) return 0;
#endif
    if (!x86_lapic_idle(owner)) return 0;
    x86_window_entries(3)[X86_LAPIC_LEAF] = 0;
    x86_invalidate_page(X86_LAPIC_VIRTUAL);
    if (x86_window_entries(3)[X86_LAPIC_LEAF]) return 0;
    clear_owner(owner);
    return 1;
}

// ====== output dword non puo aliasare owner o pagine dispositivi ===== //

static int output_range(const X86_LAPIC_MAPPING *owner, BOOT_U32 *value) {
    BOOT_U64 target = (BOOT_U64)value;
    if (!value || (target & 3) || target > ~0ULL-sizeof(*value) ||
        os_address_ranges_overlap(target,sizeof(*value),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap(target,sizeof(*value),X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(target,sizeof(*value),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(target,sizeof(*value),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES)) return 0;
    return 1;
}

// ====== offset ISR IRR bounded prima di una lettura MMIO volatile ===== //

int x86_lapic_read(const X86_LAPIC_MAPPING *owner, BOOT_U32 offset, BOOT_U32 *value)
{
    if (!output_range(owner,value) || (offset & 15) ||
        !((offset >= 0x100 && offset <= 0x170) || (offset >= 0x200 && offset <= 0x270)) ||
        !x86_lapic_validate(owner)) return 0;
    return x86_lapic_native_read(offset,value);
}

// ====== EOI ammesso solo attraverso un owner ancora collegato ===== //

// Intel SDM 078US 11.8.4/Fig11-20: vector v is ISR 100h+16*(v/32), bit v%32.
static int isr(const X86_LAPIC_MAPPING *owner, BOOT_U32 vector) {
    BOOT_U32 index, value;
    for (index = 0; index < 8; ++index)
        if (!x86_lapic_read(owner,0x100+index*16,&value) ||
            value != (vector && index == vector / 32 ? 1U << (vector % 32) : 0)) return 0;
    return 1;
}

// ====== unico ISR quaranta poi EOI zero e readback vuoto ===== //

int x86_lapic_eoi(const X86_LAPIC_MAPPING *owner) {
    return x86_lapic_eoi_vector(owner,0x40);
}

// ====== EOI solo per un vettore autorizzato realmente in servizio ===== //

// Intel SDM 078US 11.8.4/Fig11-20: vector43h = ISR120h bit3.
// 11.8.5/Fig11-21: EOI b0h=0 only for the sole authorized in-service vector.
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *owner, BOOT_U32 vector) {
    if ((vector < 0x40 || vector > 0x43) && vector != X86_PREEMPT_VECTOR) return 0;
    return isr(owner,vector) && x86_lapic_native_eoi() && isr(owner,0);
}

// ====== nessuna consegna pendente prima di arm o revoca ===== //

int x86_lapic_idle(const X86_LAPIC_MAPPING *owner) {
    return x86_lapic_idle_except(owner,0);
}

// ====== ISR vuoto e IRR limitato ai vettori autorizzati ancora pendenti ===== //

// Intel SDM 078US Vol3A, 11.8.4/Fig11-20: ISR100h..170h, IRR200h..270h.
// IRR bank2 (220h) bits0..3 = vettori40h..43h; pending bit4 = vettore D1h, IRR bank6 bit17.
int x86_lapic_idle_except(const X86_LAPIC_MAPPING *owner, BOOT_U32 pending) {
    BOOT_U32 index, value, allowed;
    if ((pending & ~31U) || !isr(owner,0)) return 0;
    for (index = 0; index < 8; ++index) {
        allowed = index == 2 ? pending & 15U : 0U;
        if (index == X86_PREEMPT_VECTOR / 32 && (pending & 16U)) allowed |= 1U << (X86_PREEMPT_VECTOR % 32);
        if (!x86_lapic_read(owner,0x200+index*16,&value) || (value & ~allowed)) return 0;
    }
    return 1;
}

// ====== fotografia del mapping ancora vivo dopo le consegne IRQ ===== //

int x86_lapic_report(const X86_LAPIC_MAPPING *owner) {
    if (!x86_lapic_validate(owner)) return 0;
    return kernel_section("mapping LAPIC dedicato al driver") &&
        kernel_text("KERNEL_LAPIC_MAPPING: ") && kernel_number((BOOT_U64)owner) &&
        kernel_text("\nKERNEL_LAPIC_VIRTUAL: ") && kernel_number(X86_LAPIC_VIRTUAL) &&
        kernel_text("\nKERNEL_LAPIC_PTE: ") &&
        kernel_number(x86_window_entries(3)[X86_LAPIC_LEAF] & ~AD) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_LAPIC_MAPPING_OK");
}
