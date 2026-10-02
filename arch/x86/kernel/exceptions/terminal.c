#include "terminal.h"
#include "kernel/diagnostics/log.h"
extern const BOOT_U64 x86_kernel_fault_targets[32];
extern const unsigned char x86_kernel_fault_begin[], x86_kernel_fault_end[];
static X86_KERNEL_IDT kernel_idt;
static X86_KERNEL_IDTR kernel_idtr;
static BOOT_U32 installed;

// ====== intervalli non sovrapposti senza overflow prima degli accessi ===== //

static int separate(BOOT_U64 first, BOOT_U64 bytes, BOOT_U64 second, BOOT_U64 other)
{
    if (first > ~0ULL - bytes || second > ~0ULL - other) return 0;
    return first + bytes <= second || second + other <= first;
}

// ====== preflight completo prima di modificare un singolo byte ===== //

static int arguments(const X86_KERNEL_IDT *table, const X86_KERNEL_IDTR *pointer,
    const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U32 vector;
    BOOT_U64 base = (BOOT_U64)table;
    if (!table || !pointer || !targets || (base & 15) || ((BOOT_U64)targets & 7) ||
        base < 4096 || base > X86_BOOT_LOW_ADDRESS_LIMIT - sizeof(*table)) return 0;
    if (!separate(base, sizeof(*table), (BOOT_U64)pointer, sizeof(*pointer)) ||
        !separate(base, sizeof(*table), (BOOT_U64)targets, 32 * sizeof(*targets)) ||
        !separate((BOOT_U64)pointer, sizeof(*pointer), (BOOT_U64)targets, 32 * sizeof(*targets))) return 0;
    for (vector = 0; vector < 32; ++vector)
        if (!x86_exception_target(targets[vector], begin, end)) return 0;
    return 1;
}

// ====== interrupt gate ring zero con IST uno gia attivo ===== //

static unsigned char gate_byte(BOOT_U64 address, BOOT_U32 byte)
{
    if (byte < 2) return (unsigned char)(address >> (byte * 8));
    if (byte == 2) return 8;
    if (byte == 4) return 1;
    if (byte == 5) return 0x8e;
    if (byte == 6 || byte == 7) return (unsigned char)(address >> ((byte - 4) * 8));
    if (byte >= 8 && byte < 12) return (unsigned char)(address >> ((byte - 4) * 8));
    return 0;
}

// ====== costruzione della IDT inattiva con confini statici ===== //

int x86_exception_prepare(X86_KERNEL_IDT *table, X86_KERNEL_IDTR *pointer,
    const BOOT_U64 targets[32], BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U32 vector, byte;
    BOOT_U64 base = (BOOT_U64)table;
    if (!arguments(table, pointer, targets, begin, end)) return 0;
    for (vector = 0; vector < 32; ++vector) {
        for (byte = 0; byte < 16; ++byte) {
            table->Gates[vector][byte] = gate_byte(targets[vector], byte);
        }
    }
    pointer->Bytes[0] = 255;
    pointer->Bytes[1] = 1;
    for (byte = 0; byte < 8; ++byte) {
        pointer->Bytes[byte + 2] = (unsigned char)(base >> (byte * 8));
    }
    return x86_exception_verify(table, pointer, targets, begin, end);
}

// ====== confronto esatto incluso IST riservati e operando IDTR ===== //

int x86_exception_verify(const X86_KERNEL_IDT *table, const X86_KERNEL_IDTR *pointer,
    const BOOT_U64 targets[32], BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U32 vector, byte;
    BOOT_U64 base = (BOOT_U64)table;
    if (!arguments(table, pointer, targets, begin, end)) return 0;
    if (pointer->Bytes[0] != 255 || pointer->Bytes[1] != 1) return 0;
    for (byte = 0; byte < 8; ++byte) {
        if (pointer->Bytes[byte + 2] != (unsigned char)(base >> (byte * 8))) return 0;
    }
    for (vector = 0; vector < 32; ++vector) {
        for (byte = 0; byte < 16; ++byte) {
            if (table->Gates[vector][byte] != gate_byte(targets[vector], byte)) return 0;
        }
    }
    return 1;
}

// ====== valore little endian nell'operando a dieci byte ===== //

static BOOT_U64 idt_base(const X86_KERNEL_IDTR *pointer)
{
    BOOT_U32 byte;
    BOOT_U64 base = 0;
    for (byte = 0; byte < 8; ++byte) {
        base |= (BOOT_U64)pointer->Bytes[byte + 2] << (byte * 8);
    }
    return base;
}

// ====== pubblicazione dopo verifica con GDT TSS e IST mantenuti dal loader ===== //

int x86_exception_initialize(void)
{
    X86_KERNEL_IDTR old = {{0}}, actual = {{0}};
    BOOT_U32 byte;
    if (installed || !x86_exception_environment()) return 0;
    x86_exception_read_idt(&old);
    if (old.Bytes[0] != 255 || old.Bytes[1] != 1 || idt_base(&old) < 4096 ||
        idt_base(&old) >= X86_BOOT_LOW_ADDRESS_LIMIT || (idt_base(&old) & 15)) return 0;
    if (!x86_exception_prepare(&kernel_idt, &kernel_idtr, x86_kernel_fault_targets,
        (BOOT_U64)x86_kernel_fault_begin, (BOOT_U64)x86_kernel_fault_end)) return 0;
    // La durata di TSS e IST resta quella del loader: le sue pagine non sono reclamate.
    x86_exception_load_idt(&kernel_idtr);
    x86_exception_read_idt(&actual);
    for (byte = 0; byte < 10; ++byte) {
        if (actual.Bytes[byte] != kernel_idtr.Bytes[byte]) {
            x86_exception_load_idt(&old);
            return 0;
        }
    }
    installed = 1;
    if (!kernel_text("KERNEL_IDT_BASE: ") || !kernel_number((BOOT_U64)&kernel_idt) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_IDT_OK")) return 0;
    return 1;
}

// ====== diagnostica terminale bounded senza allocazioni o ritorno IRET ===== //

static void fault_field(const char *name, BOOT_U64 value)
{
    if (!kernel_text(name) || !kernel_number(value) || !kernel_text("\n"))
        x86_kernel_stop(0x4b4641554c540001ULL);
}

// ====== il frame CPU e il codice d'errore restano intatti sullo stack IST ===== //

__declspec(noreturn) void x86_exception_terminal(const X86_EXCEPTION_FRAME *frame, BOOT_U64 cr2)
{
    // Prestito prodotto solo dagli ingressi assembly sullo stack IST persistente.
    // L'arresto e definitivo: registri e SIMD non vengono ripristinati.
    if (!frame || !installed) x86_kernel_stop(0x4b4641554c540001ULL);
    if (!kernel_section("eccezione CPU kernel") || !kernel_log(3, "KERNEL_EXCEPTION"))
        x86_kernel_stop(0x4b4641554c540001ULL);
    fault_field("FAULT_FRAME: ", (BOOT_U64)frame);
    fault_field("FAULT_VECTOR: ", frame->Vector);
    fault_field("FAULT_ERROR: ", frame->Error);
    fault_field("FAULT_RIP: ", frame->Rip);
    fault_field("FAULT_CS: ", frame->Cs);
    fault_field("FAULT_FLAGS: ", frame->Flags);
    fault_field("FAULT_RSP: ", frame->Rsp);
    fault_field("FAULT_SS: ", frame->Ss);
    if (frame->Vector == 14) fault_field("FAULT_CR2: ", cr2);
    if (!x86_exception_frame(frame->Vector, frame->Error, frame->Cs, frame->Flags, frame->Ss))
        kernel_log(3, "EXCEPTION_FRAME_INVALID");
    kernel_log(3, "KERNEL_EXCEPTION_HALTED");
    x86_kernel_stop(0x4b4641554c540001ULL);
}
