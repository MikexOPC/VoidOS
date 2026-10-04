#include "table.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
extern const BOOT_U64 x86_irq_targets[224];
extern const unsigned char x86_irq_begin[], x86_irq_end[];
extern const BOOT_U64 x86_kernel_fault_targets[32];
static X86_IRQ_IDT irq_idt;
static X86_KERNEL_IDTR irq_idtr;
static X86_IRQ_REGISTRY irq_registry;
static BOOT_U32 installed, armed, returned, spurious;
static int exceptions_valid(const X86_KERNEL_IDT *exceptions);

// ====== intervalli bounded disgiunti prima di costruire la tabella ===== //

static int arguments(const X86_IRQ_IDT *table, const X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U64 addresses[4] = {(BOOT_U64)table,(BOOT_U64)pointer,(BOOT_U64)exceptions,(BOOT_U64)targets};
    BOOT_U64 sizes[4] = {sizeof(*table),sizeof(*pointer),sizeof(*exceptions),224 * sizeof(*targets)};
    BOOT_U32 index, other;
    if (!x86_irq_idtr(addresses[0],4095) || (addresses[2] & 15) || (addresses[3] & 7)) return 0;
    for (index = 0; index < 4; ++index) {
        if (addresses[index] < 4096 || addresses[index] > X86_BOOT_LOW_ADDRESS_LIMIT - sizes[index]) return 0;
        for (other = 0; other < index; ++other)
            if (os_address_ranges_overlap(addresses[index],sizes[index],addresses[other],sizes[other])) return 0;
    }
    for (index = 0; index < 224; ++index)
        if (!x86_exception_target(targets[index],begin,end)) return 0;
    return exceptions_valid(exceptions);
}

// ====== ingressi ring zero con IST zero sullo stack kernel corrente ===== //

static unsigned char gate(BOOT_U64 address, BOOT_U32 byte)
{
    if (byte < 2) return (unsigned char)(address >> (byte*8));
    if (byte == 2) return 8;
    if (byte == 5) return 0x8e;
    if (byte == 6 || byte == 7) return (unsigned char)(address >> ((byte-4)*8));
    if (byte >= 8 && byte < 12) return (unsigned char)(address >> ((byte-4)*8));
    return 0;
}

// ====== i gate terminali mantengono gli indirizzi e gli IST kernel ===== //

static int exceptions_valid(const X86_KERNEL_IDT *exceptions)
{
    BOOT_U32 vector, byte;
    for (vector = 0; vector < 32; ++vector) {
        for (byte = 0; byte < 16; ++byte) {
            unsigned char expected = byte == 4 ? (vector == 2 ? 2 : vector == 8 ? 3 : 1) :
                gate(x86_kernel_fault_targets[vector],byte);
#ifdef KERNEL_TEST_DOUBLE_FAULT
            // L'immagine diagnostica preserva il gate #GP assente intenzionale.
            if (vector == 13 && byte == 5) expected &= 0x7f;
#endif
            if (exceptions->Gates[vector][byte] != expected) return 0;
        }
    }
    return 1;
}

// ====== eccezioni copiate byte per byte preservando i tre IST esistenti ===== //

int x86_irq_prepare(X86_IRQ_IDT *table, X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U32 vector, byte;
    if (!arguments(table,pointer,exceptions,targets,begin,end)) return 0;
    for (vector = 0; vector < 256; ++vector) {
        for (byte = 0; byte < 16; ++byte)
            table->Gates[vector][byte] = vector < 32 ? exceptions->Gates[vector][byte] : gate(targets[vector-32],byte);
    }
    pointer->Bytes[0] = 255; pointer->Bytes[1] = 15;
    for (byte = 0; byte < 8; ++byte) pointer->Bytes[byte+2] = (unsigned char)((BOOT_U64)table >> (byte*8));
    return x86_irq_verify(table,pointer,exceptions,targets,begin,end);
}

// ====== confronto esatto di tutti i gate e del limite IDTR ===== //

int x86_irq_verify(const X86_IRQ_IDT *table, const X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U32 vector, byte;
    if (!arguments(table,pointer,exceptions,targets,begin,end) ||
        pointer->Bytes[0] != 255 || pointer->Bytes[1] != 15) return 0;
    for (byte = 0; byte < 8; ++byte)
        if (pointer->Bytes[byte+2] != (unsigned char)((BOOT_U64)table >> (byte*8))) return 0;
    for (vector = 0; vector < 256; ++vector) {
        for (byte = 0; byte < 16; ++byte) {
            unsigned char expected = vector < 32 ? exceptions->Gates[vector][byte] : gate(targets[vector-32],byte);
            if (table->Gates[vector][byte] != expected) return 0;
        }
    }
    return 1;
}

// ====== operando IDTR ricostruito senza letture non allineate ===== //

static BOOT_U64 base(const X86_KERNEL_IDTR *pointer)
{
    BOOT_U64 result = 0;
    BOOT_U32 byte;
    for (byte = 0; byte < 8; ++byte) result |= (BOOT_U64)pointer->Bytes[byte+2] << (byte*8);
    return result;
}
static int equal(const X86_KERNEL_IDTR *first, const X86_KERNEL_IDTR *second)
{
    BOOT_U32 byte;
    for (byte = 0; byte < 10; ++byte) if (first->Bytes[byte] != second->Bytes[byte]) return 0;
    return 1;
}

// ====== caricamento con readback e rollback prima della pubblicazione ===== //

int x86_irq_activate(const X86_KERNEL_IDTR *pointer)
{
    X86_KERNEL_IDTR old = {{0}}, actual = {{0}};
    BOOT_U64 limit;
    if (!pointer || (BOOT_U64)pointer < 4096 ||
        (BOOT_U64)pointer > X86_BOOT_LOW_ADDRESS_LIMIT - sizeof(*pointer) ||
        !x86_exception_environment() || !x86_irq_idtr(base(pointer),
            pointer->Bytes[0] | ((BOOT_U64)pointer->Bytes[1] << 8))) return 0;
    x86_exception_read_idt(&old);
    limit = old.Bytes[0] | ((BOOT_U64)old.Bytes[1] << 8);
    if (!x86_exception_idtr(base(&old),limit) && !x86_irq_idtr(base(&old),limit)) return 0;
    x86_exception_load_idt(pointer); x86_exception_read_idt(&actual);
    if (equal(pointer,&actual)) return 1;
    x86_exception_load_idt(&old); x86_exception_read_idt(&actual);
    return equal(&old,&actual) ? 0 : -1;
}

// ====== installazione sullo stack kernel con stato SIMD gia utilizzabile ===== //

int x86_irq_initialize(void)
{
    X86_KERNEL_IDTR old = {{0}};
    const X86_KERNEL_IDT *exceptions;
    int result;
    if (installed || !x86_exception_environment() || !x86_irq_cpu_state()) return 0;
    x86_exception_read_idt(&old);
    if (!x86_exception_idtr(base(&old),old.Bytes[0] | ((BOOT_U64)old.Bytes[1] << 8))) return 0;
    exceptions = (const X86_KERNEL_IDT *)base(&old);
    if (!x86_irq_prepare(&irq_idt,&irq_idtr,exceptions,x86_irq_targets,(BOOT_U64)x86_irq_begin,(BOOT_U64)x86_irq_end)) return 0;
    result = x86_irq_activate(&irq_idtr);
    if (result < 0) x86_kernel_stop(0x4b49525142410001ULL);
    if (!result) return 0;
    if (!x86_irq_registry_initialize(&irq_registry,0x100000000ULL,0x100100000ULL)) return 0;
    installed = 1; armed = 1;
    result = x86_irq_return_test();
    armed = 0;
    if (!result || returned != 1) return 0;
    // INT non passa dal LAPIC: il vettore spurious deve tornare senza EOI.
    if (!x86_irq_spurious_test() || spurious != 1) return 0;
    {
        X86_KERNEL_IDTR actual = {{0}};
        x86_exception_read_idt(&actual);
        if (!equal(&actual,&irq_idtr)) return 0;
    }
    if (!result || returned != 1 || !x86_exception_environment() ||
        !x86_irq_verify(&irq_idt,&irq_idtr,exceptions,x86_irq_targets,(BOOT_U64)x86_irq_begin,(BOOT_U64)x86_irq_end)) return 0;
    if (!kernel_section("IDT e ritorno interrupt ring zero") ||
        !kernel_text("KERNEL_IRQ_IDT_BASE: ") || !kernel_number((BOOT_U64)&irq_idt) ||
        !kernel_text("\nKERNEL_IRQ_RETURN_COUNT: ") || !kernel_number(returned) ||
        !kernel_text("\nKERNEL_IRQ_SPURIOUS_COUNT: ") || !kernel_number(spurious) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_IRQ_RETURN_OK")) return 0;
    return 1;
}

// ====== frame preso in prestito dal solo stack principale persistente ===== //

static int frame_owner(const X86_IRQ_FRAME *frame)
{
    BOOT_U64 address = (BOOT_U64)frame;
    // IST zero: il frame e preso in prestito dallo stack principale persistente.
    return frame && !(address & 7) && address >= X86_KERNEL_MAIN_BASE &&
        address <= X86_KERNEL_MAIN_TOP - sizeof(*frame) &&
        frame->Rsp >= address + sizeof(*frame) && frame->Rsp <= X86_KERNEL_MAIN_TOP &&
        frame->Rip >= 0x100000000ULL && frame->Rip < 0x100100000ULL;
}

// ====== contatori bounded senza wrap durante il boot uniprocessore ===== //

static int increment(BOOT_U32 *counter)
{
    if (*counter == ~0U) return 0;
    ++*counter;
    return 1;
}

// ====== IDTR attiva e ambiente verificati prima della prova hardware ===== //

int x86_irq_ready(void)
{
    X86_KERNEL_IDTR actual = {{0}};
    if (!installed || armed || returned != 1 || !x86_exception_environment() || !x86_irq_cpu_state()) return 0;
    x86_exception_read_idt(&actual);
    return equal(&actual,&irq_idtr);
}

// ====== i vettori non assegnati terminano senza confermare IRQ sconosciuti ===== //

void x86_irq_dispatch(const X86_IRQ_FRAME *frame)
{
    if (!installed || !frame_owner(frame) || !x86_irq_frame(frame->Vector,frame->Cs,frame->Flags,frame->Ss))
        x86_kernel_stop(0x4b495251554e0001ULL);
    if (frame->Vector == 0x40 || frame->Vector == 0x41) {
        if (!x86_irq_registry_deliver(&irq_registry,(BOOT_U32)frame->Vector)) x86_kernel_stop(0x4b495251554e0004ULL);
    } else if (frame->Vector == 0xf0) {
        if (!armed || returned) x86_kernel_stop(0x4b495251554e0002ULL);
        if (!increment(&returned)) x86_kernel_stop(0x4b495251554e0003ULL);
    } else {
        if (!increment(&spurious)) x86_kernel_stop(0x4b495251554e0003ULL);
    }
}

// ====== registro persistente accessibile dopo installazione sul BSP con IF zero ===== //

X86_IRQ_REGISTRY *x86_irq_registry(void)
{
    return installed && x86_exception_environment() && x86_irq_registry_validate(&irq_registry) ? &irq_registry : 0;
}
