#ifndef OS_X86_KERNEL_MAIN_STACK_H
#define OS_X86_KERNEL_MAIN_STACK_H
#include "arch/x86/kernel/descriptors/stacks.h"
typedef struct {
    X86_KERNEL_STACKS Storage;
    BOOT_U64 LoaderPhysical, Budget;
    BOOT_U32 Phase, Reserved;
} X86_KERNEL_MAIN_STACK;
// Owner statico non spostabile; nessun puntatore o ritorno al vecchio stack.
int x86_kernel_main_prepare(X86_KERNEL_MAIN_STACK *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, const BOOT_START_INFO *info);
int x86_kernel_main_release(X86_KERNEL_MAIN_STACK *owner);
int x86_kernel_main_commit(X86_KERNEL_MAIN_STACK *owner);
int x86_kernel_main_retire(X86_KERNEL_MAIN_STACK *owner);
int x86_kernel_main_report(const X86_KERNEL_MAIN_STACK *owner);
__declspec(noreturn) void x86_kernel_main_enter(X86_KERNEL_MAIN_STACK *owner, void (*next)(void));
BOOT_U64 x86_kernel_stack_pointer(void);
// Backend: top verificato e allineato a 16; next e un ingresso statico RX senza argomenti.
__declspec(noreturn) void x86_kernel_stack_switch(BOOT_U64 top, void (*next)(void));
#endif
