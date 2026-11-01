#include "segments.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "kernel/diagnostics/log.h"
static X86_KERNEL_SEGMENTS kernel_segments;
static X86_KERNEL_STACKS kernel_stacks;

// ====== confronto bounded del campo RSP0 non allineato nel formato hardware ===== //

static int main_rsp0(const X86_BOOT_TSS *task)
{
    BOOT_U32 byte;
    for (byte = 0; byte < 8; ++byte) {
        if (task->Bytes[4 + byte] != (unsigned char)(X86_KERNEL_MAIN_TOP >> (byte*8))) return 0;
    }
    return 1;
}

// ====== owner esclusivo completo prima degli accessi ===== //

static int valid_owner(const X86_KERNEL_SEGMENTS *owner)
{
    BOOT_U64 base = (BOOT_U64)owner;
    // K1: kernel objects may sit in either canonical half (layout.h).
    if (!owner || (base & 15) || !X86_LAYOUT_RANGE(base, sizeof(*owner))) {
        return 0;
    }
    return base + sizeof(*owner) <= X86_KERNEL_IST_GUARD ||
        base >= X86_KERNEL_IST_GUARD + 16 * 4096;
}

// ====== geometria Rust confrontata con il profilo C prima dei descrittori ===== //

static int stack_layout(void)
{
    BOOT_U32 stack, slot;
    for (stack = 1; stack <= 3; ++stack) {
        BOOT_U64 base = X86_KERNEL_IST_GUARD + ((stack - 1) * 5 + 1) * 4096;
        if (x86_ist_base(stack) != base || x86_ist_top(stack) != base + X86_KERNEL_IST_BYTES) {
            return 0;
        }
    }
    for (slot = 0; slot < 16; ++slot) {
        BOOT_U32 expected = slot % 5 ? slot / 5 * 4 + slot % 5 : 0;
        if (x86_ist_page(slot) != expected) {
            return 0;
        }
    }
    return 1;
}

// ====== scrittura degli ulteriori IST senza strutture packed ===== //

static void additional_ist(X86_BOOT_TSS *task)
{
    BOOT_U32 stack, byte;
    for (stack = 2; stack <= 3; ++stack) {
        BOOT_U64 top = x86_ist_top(stack);
        for (byte = 0; byte < 8; ++byte) {
            task->Bytes[36 + (stack - 1) * 8 + byte] = (unsigned char)(top >> (byte * 8));
        }
    }
}

// ====== profilo condiviso GDT e TSS prima delle aggiunte kernel ===== //

int x86_kernel_segments_prepare(X86_KERNEL_SEGMENTS *owner)
{
    BOOT_U32 byte;
    if (!valid_owner(owner) || !stack_layout()) {
        return 0;
    }
    for (byte = 0; byte < sizeof(*owner); ++byte) {
        if (((const unsigned char *)owner)[byte]) {
            return 0;
        }
    }
    if (!x86_prepare_boot_gdt(&owner->Gdt, &owner->Pointer, (BOOT_U64)&owner->Gdt) ||
        !x86_prepare_boot_tss(&owner->Task, &owner->Descriptor, (BOOT_U64)&owner->Task,
            X86_BOOT_STACK_TOP - X86_KERNEL_IST_BYTES, x86_ist_base(1))) {
        return 0;
    }
    owner->Pointer.Bytes[0] = 39;
    additional_ist(&owner->Task);
    return x86_kernel_segments_verify(owner, 0);
}

// ====== TSS busy solo dopo LTR senza mascherare altri bit ===== //

int x86_kernel_segments_verify(const X86_KERNEL_SEGMENTS *owner, BOOT_U32 busy)
{
    X86_GDT_POINTER pointer;
    X86_TSS_DESCRIPTOR descriptor;
    X86_BOOT_TSS task;
    BOOT_U32 byte, stack;
    if (!valid_owner(owner) || !stack_layout() || busy > 1 || owner->Pointer.Bytes[0] != 39 ||
        owner->Descriptor.Bytes[5] != (busy ? 0x8b : 0x89)) {
        return 0;
    }
    for (byte = 0; byte < 10; ++byte) {
        pointer.Bytes[byte] = owner->Pointer.Bytes[byte];
    }
    pointer.Bytes[0] = 23;
    for (byte = 0; byte < 16; ++byte) {
        descriptor.Bytes[byte] = owner->Descriptor.Bytes[byte];
    }
    descriptor.Bytes[5] = 0x89;
    for (byte = 0; byte < 104; ++byte) {
        task.Bytes[byte] = owner->Task.Bytes[byte];
    }
    if (busy && main_rsp0(&task)) {
        for (byte = 0; byte < 8; ++byte) task.Bytes[4 + byte] = (unsigned char)(X86_BOOT_STACK_TOP >> (byte*8));
    }
    for (stack = 2; stack <= 3; ++stack) {
        BOOT_U64 top = x86_ist_top(stack);
        for (byte = 0; byte < 8; ++byte) {
            BOOT_U32 offset = 36 + (stack - 1) * 8 + byte;
            if (task.Bytes[offset] != (unsigned char)(top >> (byte * 8))) {
                return 0;
            }
            task.Bytes[offset] = 0;
        }
    }
    return x86_validate_boot_gdt(&owner->Gdt, &pointer, (BOOT_U64)&owner->Gdt) &&
        x86_validate_boot_tss(&task, &descriptor, (BOOT_U64)&owner->Task,
            X86_BOOT_STACK_TOP - X86_KERNEL_IST_BYTES, x86_ist_base(1));
}

// ====== diagnostica verificata per prove indipendenti del monitor QEMU ===== //

static int field(const char *name, BOOT_U64 value)
{
    if (!kernel_text(name) || !kernel_number(value)) {
        return 0;
    }
    return kernel_text("\n");
}

// ====== verifica dello stato effettivo dopo le istruzioni privilegiate ===== //

static int loaded(void)
{
    X86_GDT_POINTER actual = {{0}};
    BOOT_U32 byte;
    x86_kernel_segments_read(&actual);
    for (byte = 0; byte < sizeof(actual); ++byte) {
        if (actual.Bytes[byte] != kernel_segments.Pointer.Bytes[byte]) {
            return 0;
        }
    }
    // SGDT controlla il registro, il bit busy controlla la scrittura di LTR.
    // Il backend reale e l'unico chiamante privilegiato; i test QEMU verificano
    // anche la cache nascosta TR, non leggibile con la sola istruzione STR.
    return x86_exception_environment() != 0;
}

// ====== budget del solo profilo IST dopo il primo LTR ===== //

static int activated(BOOT_U64 budget)
{
    return loaded() &&
        x86_kernel_segments_verify(&kernel_segments, 1) &&
        x86_kernel_stacks_validate(&kernel_stacks) &&
        budget >= 12 && kernel_physical_budget_matches(kernel_stacks.Pool, budget - 12);
}

// ====== aggiornamento RSP0 in memoria senza ripetere LTR su un descrittore busy ===== //

int x86_kernel_segments_retarget(X86_KERNEL_SEGMENTS *owner)
{
    BOOT_U32 byte;
    if (!x86_kernel_segments_verify(owner, 1) || main_rsp0(&owner->Task)) return 0;
    // UP, CPL0 e IF=0: nessun ingresso di privilegio legge RSP0 durante la scrittura.
    // NMI e DF usano IST separati. Il chiamante deve mantenere queste precondizioni.
    for (byte = 0; byte < 8; ++byte) owner->Task.Bytes[4 + byte] = (unsigned char)(X86_KERNEL_MAIN_TOP >> (byte*8));
    return x86_kernel_segments_verify(owner, 1) && main_rsp0(&owner->Task);
}

// ====== owner principale pin-nato nello stesso pool prima della pubblicazione ===== //

int x86_kernel_segments_bind_main(const X86_KERNEL_STACKS *main)
{
    if (!x86_kernel_stacks_validate(main) || main->Profile != 2 || !main->Committed ||
        !x86_kernel_stacks_validate(&kernel_stacks) || !kernel_stacks.Committed ||
        main->Pool != kernel_stacks.Pool || !loaded() ||
        !x86_kernel_segments_retarget(&kernel_segments)) return 0;
    return field("KERNEL_RSP0: ", X86_KERNEL_MAIN_TOP) && kernel_log(0, "KERNEL_RSP0_OK");
}

// ====== x86_kernel_segments_retire_ready ======

int x86_kernel_segments_retire_ready(void) {
    return X86_KERNEL_IMAGE_CONTAINS((BOOT_U64)&kernel_segments, sizeof(kernel_segments)) &&
        loaded() && x86_kernel_segments_verify(&kernel_segments, 1) && main_rsp0(&kernel_segments.Task) &&
        x86_kernel_stacks_validate(&kernel_stacks) && kernel_stacks.Committed;
}

// ====== describe ======

static int describe(void)
{
    BOOT_U32 stack;
    const char *names[] = {"KERNEL_IST1_TOP: ", "KERNEL_IST2_TOP: ", "KERNEL_IST3_TOP: "};
    if (!field("KERNEL_GDT_BASE: ", (BOOT_U64)&kernel_segments.Gdt) ||
        !field("KERNEL_TSS_BASE: ", (BOOT_U64)&kernel_segments.Task) ||
        !field("KERNEL_IST_PHYSICAL: ", kernel_stacks.Lease.Base) ||
        !field("KERNEL_IST_PTE: ", (BOOT_U64)(x86_stack_entries(3) + X86_KERNEL_IST_LEAF))) {
        return 0;
    }
    for (stack = 1; stack <= 3; ++stack) {
        if (!field(names[stack - 1], x86_ist_top(stack))) {
            return 0;
        }
    }
    return 1;
}

// ====== pin permanente prima che LTR renda raggiungibili i nuovi IST ===== //

int x86_kernel_segments_initialize(KERNEL_PHYSICAL_ALLOCATOR *pool)
{
    BOOT_U64 budget;
    if (!pool || !kernel_physical_validate(pool) || !x86_exception_environment()) {
        return 0;
    }
    if (!kernel_physical_budget_read(pool, &budget)) return 0;
    // Il blocco unico evita frammenti e rende esatto il consumo di dodici pagine.
    // L'owner statico resta alla stessa posizione fino all'arresto del bootstrap.
    if (!x86_kernel_stacks_acquire(&kernel_stacks, pool, x86_kernel_root())) {
        return 0;
    }
    if (!x86_kernel_segments_prepare(&kernel_segments)) {
        // Prima di LTR si puo ancora revocare il mapping e restituire il lease.
        // Il chiamante interrompe il bootstrap; non ritenta descrittori parziali.
        x86_kernel_stacks_release(&kernel_stacks);
        return 0;
    }
    if (!x86_kernel_stacks_commit(&kernel_stacks)) {
        x86_kernel_stacks_release(&kernel_stacks);
        return 0;
    }
    // Dopo questo punto gli stack non vengono liberati nemmeno su errore.
    // Nessuna chiamata al firmware e nessuna restituzione delle sue pagine.
    // RSP0 usa ancora lo stack principale ricevuto nel contratto di boot.
    x86_kernel_segments_activate(&kernel_segments.Pointer);
    if (!activated(budget) || !x86_exception_split_ist() || !describe()) {
        // Fail closed: il chiamante passa al panic con lo storage ancora vivo.
        return 0;
    }
    // Le gate NMI e DF usano IST2 e IST3 solo dopo la verifica della TSS attiva.
    // Il normale kernel non abilita IF e non aggiunge interrupt esterni qui.
    return kernel_log(0, "KERNEL_DESCRIPTORS_OK");
}
