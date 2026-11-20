#include "table.h"
#include "arch/x86/configs/layout.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#ifdef KERNEL_AP_RUNTIME
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/scheduler/irq/preempt.h"
#endif
#ifdef KERNEL_SCHEDULER_IRQ
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "arch/x86/kernel/scheduler/context.h"
#endif
extern const BOOT_U64 x86_irq_targets[224];
extern const unsigned char x86_irq_begin[], x86_irq_end[];
extern const BOOT_U64 x86_kernel_fault_targets[32];
static X86_IRQ_IDT irq_idt;
static X86_KERNEL_IDTR irq_idtr;
typedef __declspec(align(64)) struct {
    X86_IRQ_REGISTRY       Registry;
    X86_IRQ_TOKEN          Service, Timer;
    const X86_IRQ_FRAME   *Frame;
    BOOT_U32              Ready;
    BOOT_U32              Reserved[11];
} X86_IRQ_CPU;
_Static_assert(sizeof(X86_IRQ_CPU) % 64 == 0, "IRQ ownership uses separate cache lines");

static X86_IRQ_CPU irq_cpus[X86_PERCPU_CAPACITY];
static const X86_KERNEL_IDT *irq_exceptions;
static BOOT_U32 installed, armed, returned, spurious;
static int exceptions_valid(const X86_KERNEL_IDT *exceptions);

// ====== local_private ======

static X86_IRQ_CPU *local_private(void) {
#ifdef KERNEL_AP_RUNTIME
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner || owner->Index >= X86_PERCPU_CAPACITY || owner->State != X86_PERCPU_ACTIVE ||
        owner->Self != (BOOT_U64)owner) return NULL;
    return &irq_cpus[owner->Index];
#else
    return &irq_cpus[0];
#endif
}

// ====== intervalli bounded disgiunti prima di costruire la tabella ===== //

static int arguments(const X86_IRQ_IDT *table, const X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end)
{
    BOOT_U64 addresses[4] = {(BOOT_U64)table,(BOOT_U64)pointer,(BOOT_U64)exceptions,(BOOT_U64)targets};
    BOOT_U64 sizes[4] = {sizeof(*table),sizeof(*pointer),sizeof(*exceptions),224 * sizeof(*targets)};
    BOOT_U32 index, other;
    if (!x86_irq_idtr(addresses[0],4095) || (addresses[2] & 15) || (addresses[3] & 7)) return 0;
    for (index = 0; index < 4; ++index) {
        if (!X86_LAYOUT_RANGE(addresses[index], sizes[index])) return 0;
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
    if (!pointer || !X86_LAYOUT_RANGE((BOOT_U64)pointer, sizeof(*pointer)) ||
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
    X86_IRQ_CPU *local = local_private();
    if (!local || installed || !x86_exception_environment() || !x86_irq_cpu_state()) return 0;
    x86_exception_read_idt(&old);
    if (!x86_exception_idtr(base(&old),old.Bytes[0] | ((BOOT_U64)old.Bytes[1] << 8))) return 0;
    exceptions = (const X86_KERNEL_IDT *)base(&old);
    irq_exceptions = exceptions;
    if (!x86_irq_prepare(&irq_idt,&irq_idtr,exceptions,x86_irq_targets,(BOOT_U64)x86_irq_begin,(BOOT_U64)x86_irq_end)) return 0;
    result = x86_irq_activate(&irq_idtr);
    if (result < 0) x86_kernel_stop(0x4b49525142410001ULL);
    if (!result) return 0;
    if (!x86_irq_registry_initialize(&local->Registry,X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT)) return 0;
    local->Ready = 1;
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
        !kernel_text("\nKERNEL_IRQ_REGISTRY_BYTES: ") || !kernel_number(sizeof(X86_IRQ_REGISTRY)) ||
        !kernel_text("\nKERNEL_IRQ_CPUS: ") || !kernel_number((BOOT_U64)irq_cpus) ||
        !kernel_text("\nKERNEL_IRQ_CPU_BYTES: ") || !kernel_number(sizeof(X86_IRQ_CPU)) ||
        !kernel_text("\nKERNEL_IRQ_RETURN_COUNT: ") || !kernel_number(returned) ||
        !kernel_text("\nKERNEL_IRQ_SPURIOUS_COUNT: ") || !kernel_number(spurious) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_IRQ_RETURN_OK")) return 0;
    return 1;
}

// ====== x86_irq_descriptor ======

const X86_KERNEL_IDTR *x86_irq_descriptor(void) {
    return installed ? &irq_idtr : NULL;
}

// ====== x86_irq_shared_valid ======

int x86_irq_shared_valid(void) {
    return installed && irq_exceptions && x86_irq_verify(&irq_idt,&irq_idtr,irq_exceptions,
        x86_irq_targets,(BOOT_U64)x86_irq_begin,(BOOT_U64)x86_irq_end);
}

// ====== x86_irq_cpu_activate ======

int x86_irq_cpu_activate(void) {
#ifdef KERNEL_AP_RUNTIME
    X86_IRQ_CPU *local = local_private();
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!local || !owner || !owner->Index || local->Ready || !x86_irq_shared_valid() ||
        !x86_irq_cpu_state() ||
        !x86_irq_registry_initialize(&local->Registry,X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT) ||
        !xis_irq_bind(&local->Registry,&local->Service,&local->Timer) ||
        x86_irq_activate(&irq_idtr) != 1) return 0;
    local->Ready = 1;
    return 1;
#else
    return 0;
#endif
}

// ====== x86_irq_current_frame ======

const X86_IRQ_FRAME *x86_irq_current_frame(void) {
    X86_IRQ_CPU *local = local_private();
    return local && local->Ready ? local->Frame : NULL;
}

// ====== frame preso in prestito da stack principale o task attiva pinned ===== //

static int frame_owner(const X86_IRQ_FRAME *frame)
{
#ifdef KERNEL_AP_RUNTIME
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner) return 0;
    if (owner->Index) return xis_irq_frame_valid(frame);
#endif
    BOOT_U64 address = (BOOT_U64)frame;
    BOOT_U64 floor = X86_KERNEL_MAIN_BASE, top = X86_KERNEL_MAIN_TOP;
#ifdef KERNEL_SCHEDULER_IRQ
    if (address < floor || address > top - sizeof(*frame)) {
        top = x86_scheduler_irq_stack_top(address);
        if (!top) return 0;
        floor = top - X86_TASK_STACK_BYTES;
    }
#endif
    if (!x86_irq_saved_area(address, floor, top)) return 0;
    return frame->Rsp >= address + sizeof(*frame) && frame->Rsp <= top &&
        frame->Rip >= X86_KERNEL_IMAGE_BASE && frame->Rip < X86_KERNEL_IMAGE_LIMIT;
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
    X86_IRQ_CPU *local = local_private();
    if (!local || !local->Ready || !installed || armed || returned != 1 ||
        !x86_exception_environment() || !x86_irq_cpu_state()) return 0;
    x86_exception_read_idt(&actual);
    return equal(&actual,&irq_idtr);
}

// ====== i vettori non assegnati terminano senza confermare IRQ sconosciuti ===== //

void x86_irq_dispatch(const X86_IRQ_FRAME *frame)
{
    X86_IRQ_CPU *local = local_private();
    if (!local || !local->Ready || local->Frame || !installed || !frame_owner(frame) ||
        !x86_irq_frame(frame->Vector,frame->Cs,frame->Flags,frame->Ss))
        x86_kernel_stop(0x4b495251554e0001ULL);
    local->Frame = frame;
#ifdef KERNEL_AP_RUNTIME
    if (x86_percpu_current_local()->Index) {
        int delivered = frame->Vector == 0xff ? xis_irq_handle() :
            x86_irq_registry_deliver(&local->Registry,(BOOT_U32)frame->Vector);
        local->Frame = NULL;
        if (!delivered) x86_kernel_stop(0x4b495251554e0004ULL);
        return;
    }
#endif
#ifdef KERNEL_SCHEDULER_IRQ
    if ((frame->Rsp < X86_KERNEL_MAIN_BASE || frame->Rsp > X86_KERNEL_MAIN_TOP) &&
        !x86_scheduler_irq_record(frame->Vector,frame->Rip,frame->Rsp,frame->Flags))
        x86_kernel_stop(0x4b495251554e0005ULL);
#endif
    if (frame->Vector == 0xf0) {
        if (!armed || returned) x86_kernel_stop(0x4b495251554e0002ULL);
        if (!increment(&returned)) x86_kernel_stop(0x4b495251554e0003ULL);
    } else if (frame->Vector == 0xff) {
        if (!increment(&spurious)) x86_kernel_stop(0x4b495251554e0003ULL);
    } else if (!x86_irq_registry_deliver(&local->Registry,(BOOT_U32)frame->Vector))
        x86_kernel_stop(0x4b495251554e0004ULL);
    local->Frame = NULL;
}

// ====== x86_irq_after ======

void x86_irq_after(const X86_IRQ_FRAME *frame)
{
#ifdef KERNEL_AP_RUNTIME
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (owner && owner->Index) {
        xis_irq_after(frame);
        return;
    }
#endif
#ifdef KERNEL_PS2_INPUT
    if (frame->Vector == X86_PREEMPT_VECTOR) {
        x86_preempt_after(frame);
        return;
    }
#endif
#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
    x86_scheduler_irq_after(frame);
#else
    (void)frame;
#endif
}

// ====== registro persistente accessibile dopo installazione sul BSP con IF zero ===== //

X86_IRQ_REGISTRY *x86_irq_registry(void)
{
    X86_IRQ_CPU *local = local_private();
    return local && local->Ready && installed && x86_exception_environment() &&
        x86_irq_registry_validate(&local->Registry) ? &local->Registry : NULL;
}
