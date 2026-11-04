#ifndef OS_X86_IRQ_RETURN_H
#define OS_X86_IRQ_RETURN_H
#include "arch/x86/kernel/exceptions/terminal.h"
typedef struct { _Alignas(16) unsigned char Gates[256][16]; } X86_IRQ_IDT;
typedef struct { BOOT_U64 Vector, Rip, Cs, Flags, Rsp, Ss; } X86_IRQ_FRAME;
_Static_assert(sizeof(X86_IRQ_IDT) == 4096, "IDT completa a 256 vettori");
_Static_assert(sizeof(X86_IRQ_FRAME) == 48, "Frame IRQ senza error code CPU");
BOOT_U32 x86_irq_idtr(BOOT_U64 base, BOOT_U64 limit);
BOOT_U32 x86_irq_saved_area(BOOT_U64 frame, BOOT_U64 floor, BOOT_U64 top);
BOOT_U32 x86_irq_frame(BOOT_U64 vector, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 ss);
int x86_irq_prepare(X86_IRQ_IDT *table, X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end);
int x86_irq_verify(const X86_IRQ_IDT *table, const X86_KERNEL_IDTR *pointer,
    const X86_KERNEL_IDT *exceptions, const BOOT_U64 *targets, BOOT_U64 begin, BOOT_U64 end);
int x86_irq_activate(const X86_KERNEL_IDTR *pointer);
int x86_irq_initialize(void);
int x86_irq_ready(void);
int x86_irq_cpu_state(void);
int x86_irq_return_test(void);
int x86_irq_spurious_test(void);
void x86_irq_dispatch(const X86_IRQ_FRAME *frame);
#endif
