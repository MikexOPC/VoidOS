#ifndef X86_PAGING_RETIRE_H
#define X86_PAGING_RETIRE_H
#include "../inventory.h"
#include <stddef.h>
typedef struct {
    BOOT_U64  Cr0, Root, Cr4, Efer, Flags, Rip, Rsp;
    BOOT_U64  GdtBase, GdtLimit, IdtBase, IdtLimit, Tr, Cs, Ss, Ldt;
} X86_RETIRE_CPU;
_Static_assert(sizeof(X86_RETIRE_CPU) == 120 && offsetof(X86_RETIRE_CPU, Flags) == 32 &&
    offsetof(X86_RETIRE_CPU, GdtBase) == 56 && offsetof(X86_RETIRE_CPU, Ldt) == 112, "Retirement capture ABI");
int x86_retire_cpu_valid(const X86_RETIRE_CPU *cpu, BOOT_U64 root);
int x86_paging_retire_loader(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool);
int x86_retire_cpu(X86_RETIRE_CPU *cpu);
int x86_retire_loaded(void);
volatile BOOT_U64 *x86_retire_table(BOOT_U64 view);
void x86_retire_capture(X86_RETIRE_CPU *cpu);
int x86_retire_commit(volatile BOOT_U64 *root, BOOT_U64 expected_root, BOOT_U64 expected_entry, BOOT_U64 expected_cr4);
#endif
