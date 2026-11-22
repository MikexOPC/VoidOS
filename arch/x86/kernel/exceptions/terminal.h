#ifndef OS_X86_KERNEL_EXCEPTIONS_H
#define OS_X86_KERNEL_EXCEPTIONS_H
#include "arch/x86/kernel/platform.h"
typedef struct { _Alignas(16) unsigned char Gates[32][16]; } X86_KERNEL_IDT;
typedef struct { unsigned char Bytes[10]; } X86_KERNEL_IDTR;
typedef struct {
    BOOT_U64 Vector, Error, Rip, Cs, Flags, Rsp, Ss;
} X86_EXCEPTION_FRAME;
_Static_assert(sizeof(X86_KERNEL_IDT) == 512, "IDT eccezioni ring zero");
_Static_assert(_Alignof(X86_KERNEL_IDT) == 16, "Allineamento dei gate x64");
_Static_assert(sizeof(X86_KERNEL_IDTR) == 10, "Operando IDTR senza padding");
_Static_assert(sizeof(X86_EXCEPTION_FRAME) == 56, "Frame normalizzato con IST");
int x86_exception_prepare(X86_KERNEL_IDT *table, X86_KERNEL_IDTR *pointer,
    const BOOT_U64 *targets, BOOT_U32 count, BOOT_U64 begin, BOOT_U64 end);
int x86_exception_verify(const X86_KERNEL_IDT *table, const X86_KERNEL_IDTR *pointer,
    const BOOT_U64 *targets, BOOT_U32 count, BOOT_U64 begin, BOOT_U64 end);
int x86_exception_initialize(void);
// Richiede la TSS kernel verificata e gia caricata con tutti e tre gli IST.
int x86_exception_split_ist(void);
int x86_exception_retire_ready(void);
int x86_exception_activate(const X86_KERNEL_IDTR *pointer);
BOOT_U32 x86_exception_target(BOOT_U64 address, BOOT_U64 begin, BOOT_U64 end);
BOOT_U32 x86_exception_error(BOOT_U64 vector);
BOOT_U32 x86_exception_idtr(BOOT_U64 base, BOOT_U64 limit);
BOOT_U32 x86_exception_frame(BOOT_U64 vector, BOOT_U64 error, BOOT_U64 cs, BOOT_U64 flags, BOOT_U64 ss);
void x86_exception_read_idt(X86_KERNEL_IDTR *pointer);
void x86_exception_load_idt(const X86_KERNEL_IDTR *pointer);
BOOT_U64 x86_exception_environment(void);
__declspec(noreturn) void x86_exception_terminal(const X86_EXCEPTION_FRAME *frame, BOOT_U64 cr2);
extern volatile BOOT_U64 x86_user_fault_active;
extern BOOT_U64 x86_user_leave_target;
extern BOOT_U64 x86_user_copy_recover_target;
extern BOOT_U64 x86_user_irq_capture_target;
extern BOOT_U64 x86_user_fx_clean_target;
#endif
