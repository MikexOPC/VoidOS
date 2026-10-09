#include "session.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

// ====== x86_ps2_handle ======

int x86_ps2_handle(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    BOOT_U32 count;
    unsigned char status, byte;
    if (!owner->Active || owner->Irqs == ~0U) return 0;
    ++owner->Irqs;
    /* QEMU pckbd.c status: OBF0, AUX5, timeout6/parity7. Read data60h
       only after OBF; 32-byte ISR budget, no allocation/logging/decoder. */
    for (count = 0; count < 32; ++count) {
        status = x86_read_port(0x64);
        if (status & 0xe0) { owner->Error = 1; break; }
        if (!(status & 1)) break;
        byte = x86_read_port(0x60);
        if (!owner->Quiet) {
            BOOT_U32 pushed = kernel_scheduler_payload_push(4,(BOOT_U64)owner,owner->Binding,byte);
            if (!pushed || owner->Bytes == ~0U) { owner->Error = 1; break; }
            ++owner->Bytes; owner->Ready = 1;
        }
    }
    if (count == 32 && (x86_read_port(0x64) & 1)) owner->Error = 1;
    /* Existing LAPIC helper verifies ISR vector41 before EOI (SDM Vol3
       §10.8.5). Keep source ownership through acknowledgement. */
    return !owner->Error && x86_lapic_eoi_vector(owner->Env.Mapping,0x41);
}

// ====== x86_ps2_quiet ======

int x86_ps2_quiet(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    return owner->RouteOwned && x86_ps2_route_apply(owner,1);
}

// ====== x86_ps2_irq_record ======

int x86_ps2_irq_record(BOOT_U64 vector, BOOT_U64 rip, BOOT_U64 rsp, BOOT_U64 flags)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    if ((vector != 0x40 && vector != 0x41) || x86_scheduler_index() != 1 ||
        !x86_scheduler_task_valid() || !(flags & 512) || (flags & 1024) ||
        rip < (BOOT_U64)x86_scheduler_irq_compute_begin || rip >= (BOOT_U64)x86_scheduler_irq_compute_end ||
        rsp < x86_task_stack_base(1) + 4096 || rsp >= x86_task_stack_base(1) + 65536 ||
        !x86_scheduler_code_valid(rip) || owner->TaskIrqs == ~0U) return 0;
    if (vector == 0x41) ++owner->TaskIrqs;
    return 1;
}
