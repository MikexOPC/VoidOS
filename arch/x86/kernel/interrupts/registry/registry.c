#include "registry.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/window/page.h"

// ====== memoria kernel fidata allineata prima delle letture ===== //

static int range(const void *pointer, BOOT_U64 bytes)
{
    BOOT_U64 address = (BOOT_U64)pointer;
    // Il chiamante garantisce mapping kernel leggibile e scrivibile, non puntatori utente.
    // K1: kernel objects may sit in either canonical half (layout.h).
    return !(address & 7) && X86_LAYOUT_RANGE(address, bytes) &&
        !os_address_ranges_overlap(address,bytes,X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,bytes,X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,bytes,X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES);
}

// ====== azzeramento e verifica senza runtime o allocazioni ===== //

static void clear(void *pointer, BOOT_U32 bytes)
{
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte) ((volatile unsigned char *)pointer)[byte] = 0;
}

// ====== controllo byte per byte di record o token vergini ===== //

static int zero(const void *pointer, BOOT_U32 bytes)
{
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte)
        if (((const unsigned char *)pointer)[byte]) return 0;
    return 1;
}

// ====== callback di durata kernel contenute nell'intervallo codice fidato ===== //

static int callback(const X86_IRQ_REGISTRY *registry, X86_IRQ_CALLBACK function)
{
    BOOT_U64 address = (BOOT_U64)function;
    return address >= registry->CodeBegin && address < registry->CodeEnd;
}

// ====== slot fisico distinto per COM1 e futuro timer ===== //

static BOOT_U32 slot(BOOT_U32 vector)
{
    return vector >= 0x40 && vector <= 0x42 ? vector - 0x40 : 3;
}

// ====== registro e ogni subscription verificati prima di invocare callback ===== //

int x86_irq_registry_validate(const X86_IRQ_REGISTRY *registry)
{
    BOOT_U32 index;
    if (!range(registry,sizeof(*registry)) || registry->Self != (BOOT_U64)registry ||
        registry->CodeEnd <= registry->CodeBegin ||
        !X86_LAYOUT_RANGE(registry->CodeBegin, registry->CodeEnd - registry->CodeBegin) || !registry->Next || registry->Busy > 1 || registry->Reserved) {
        return 0;
    }
    for (index = 0; index < 3; ++index) {
        const X86_IRQ_SUBSCRIPTION *item = &registry->Slots[index];
        if (!item->Id) {
            if (!zero(item,sizeof(*item))) {
                return 0;
            }
            continue;
        }
        if (!x86_irq_subscription_fields(item->Vector,item->Fault,item->Id,registry->Next) ||
            slot(item->Vector) != index || !callback(registry,item->Handle) ||
            !callback(registry,item->Quiet)) {
            return 0;
        }
    }
    for (index = 0; index < 3; ++index)
        for (BOOT_U32 other = index + 1; other < 3; ++other)
            if (registry->Slots[index].Id && registry->Slots[index].Id == registry->Slots[other].Id) return 0;
    return 1;
}

// ====== owner vergine senza riinizializzare subscription ancora vive ===== //

int x86_irq_registry_initialize(X86_IRQ_REGISTRY *registry, BOOT_U64 begin, BOOT_U64 end)
{
    if (!x86_exception_environment() || !range(registry,sizeof(*registry)) ||
        end <= begin || !X86_LAYOUT_RANGE(begin, end - begin) || !zero(registry,sizeof(*registry))) {
        return 0;
    }
    registry->Self = (BOOT_U64)registry;
    registry->CodeBegin = begin;
    registry->CodeEnd = end;
    registry->Next = 1;
    return x86_irq_registry_validate(registry);
}

// ====== token posseduto distinto dai record del registro ===== //

static int token_valid(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token)
{
    return range(token,sizeof(*token)) &&
        !os_address_ranges_overlap((BOOT_U64)registry,sizeof(*registry),(BOOT_U64)token,sizeof(*token));
}

// ====== operazioni di controllo solo IF zero e fuori da una callback ===== //

static int control(const X86_IRQ_REGISTRY *registry)
{
    return x86_exception_environment() && x86_irq_registry_validate(registry) && !registry->Busy;
}

// ====== quiescenza senza rientrare nel registro durante la verifica ===== //

static int quiet(X86_IRQ_REGISTRY *registry, X86_IRQ_CALLBACK function)
{
    int result;
    registry->Busy = 1;
    result = function();
    registry->Busy = 0;
    return result == 1 && x86_exception_environment() && x86_irq_registry_validate(registry);
}

// ====== pubblicazione prima di aprire la sorgente senza sostituzioni implicite ===== //

int x86_irq_registry_bind(X86_IRQ_REGISTRY *registry, BOOT_U32 vector,
    X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK is_quiet, X86_IRQ_TOKEN *token)
{
    BOOT_U32 index = slot(vector);
    X86_IRQ_SUBSCRIPTION *item;
    if (!control(registry) || index >= 3 || !token_valid(registry,token) ||
        !zero(token,sizeof(*token)) || !callback(registry,handle) || !callback(registry,is_quiet) ||
        registry->Next == ~0ULL || registry->Slots[index].Id) {
        return 0;
    }
    if (!quiet(registry,is_quiet)) {
        return 0;
    }
    item = &registry->Slots[index];
    item->Id = registry->Next++;
    item->Handle = handle;
    item->Quiet = is_quiet;
    item->Vector = vector;
    token->Id = item->Id;
    token->Vector = vector;
    return 1;
}

// ====== identita generazionale impedisce che un vecchio token rimuova nuovi handler ===== //

static const X86_IRQ_SUBSCRIPTION *lookup(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token)
{
    BOOT_U32 index;
    if (!token_valid(registry,token) || token->Reserved || !token->Id) return 0;
    index = slot(token->Vector);
    if (index >= 3 || registry->Slots[index].Id != token->Id) return 0;
    return &registry->Slots[index];
}

// ====== rimozione dopo sorgente chiusa e stato dispositivo quiescente ===== //

int x86_irq_registry_unbind(X86_IRQ_REGISTRY *registry, X86_IRQ_TOKEN *token)
{
    const X86_IRQ_SUBSCRIPTION *item;
    BOOT_U32 index;
    if (!control(registry)) return 0;
    item = lookup(registry,token);
    if (!item || !quiet(registry,item->Quiet)) {
        return 0;
    }
    index = slot(token->Vector);
    clear(&registry->Slots[index],sizeof(registry->Slots[index]));
    clear(token,sizeof(*token));
    return 1;
}

// ====== consegna bounded e fault persistente senza EOI generici ===== //

int x86_irq_registry_deliver(X86_IRQ_REGISTRY *registry, BOOT_U32 vector)
{
    BOOT_U32 index = slot(vector);
    X86_IRQ_SUBSCRIPTION *item;
    int result;
    if (!control(registry) || index >= 3) return 0;
    item = &registry->Slots[index];
    if (!item->Id || item->Fault || item->Delivered == ~0ULL) return 0;
    registry->Busy = 1;
    // L'handler conferma dispositivo ed EOI; zero non e mai una consegna riuscita.
    result = item->Handle();
    registry->Busy = 0;
    if (result != 1 || !x86_exception_environment() || !x86_irq_registry_validate(registry)) {
        item->Fault = 1;
        return 0;
    }
    ++item->Delivered;
    return 1;
}

// ====== fotografia del contatore soltanto per una subscription corrente ===== //

BOOT_U64 x86_irq_registry_deliveries(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token)
{
    const X86_IRQ_SUBSCRIPTION *item;
    if (!control(registry)) return 0;
    item = lookup(registry,token);
    return item ? item->Delivered : 0;
}

// ====== token corrente senza fault prima di riarmare una sorgente ===== //

int x86_irq_registry_token_live(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token)
{
    const X86_IRQ_SUBSCRIPTION *item;
    if (!control(registry)) return 0;
    item = lookup(registry,token);
    return item && !item->Fault;
}
