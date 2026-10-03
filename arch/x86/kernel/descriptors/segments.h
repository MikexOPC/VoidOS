#ifndef OS_X86_KERNEL_SEGMENTS_H
#define OS_X86_KERNEL_SEGMENTS_H
#include "stacks.h"
#include "arch/x86/lib/descriptors/gdt.h"
#include "arch/x86/lib/descriptors/tss.h"
#include <stddef.h>
typedef struct {
    _Alignas(16) X86_BOOT_GDT Gdt;
    X86_TSS_DESCRIPTOR Descriptor;
    unsigned char AlignmentPadding[8];
    _Alignas(16) X86_BOOT_TSS Task;
    X86_GDT_POINTER Pointer;
    unsigned char TailPadding[14];
} X86_KERNEL_SEGMENTS;
_Static_assert(offsetof(X86_KERNEL_SEGMENTS, Descriptor) == 24, "TSS immediatamente dopo i descrittori ring zero");
_Static_assert(_Alignof(X86_KERNEL_SEGMENTS) == 16, "GDT e TSS allineate");
_Static_assert(offsetof(X86_KERNEL_SEGMENTS, Task) == 48, "TSS allineata senza padding implicito");
_Static_assert(offsetof(X86_KERNEL_SEGMENTS, Pointer) == 152, "TSS hardware di 104 byte");
_Static_assert(sizeof(X86_KERNEL_SEGMENTS) == 176, "Storage persistente bounded");
int x86_kernel_segments_prepare(X86_KERNEL_SEGMENTS *owner);
int x86_kernel_segments_verify(const X86_KERNEL_SEGMENTS *owner, BOOT_U32 busy);
int x86_kernel_segments_initialize(KERNEL_PHYSICAL_ALLOCATOR *pool);
void x86_kernel_segments_activate(const X86_GDT_POINTER *pointer);
void x86_kernel_segments_read(X86_GDT_POINTER *pointer);
#endif
