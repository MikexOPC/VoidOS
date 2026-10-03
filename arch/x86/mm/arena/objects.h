#ifndef OS_X86_MAPPED_OBJECT_ARENA_H
#define OS_X86_MAPPED_OBJECT_ARENA_H
#include "arch/x86/mm/regions/mapping.h"
#include "kernel/mm/objects/pool.h"
typedef struct {
    BOOT_U64 Self;
    X86_REGION_MAPPER *Mapper;
    X86_REGION_HANDLE Region;
    KERNEL_OBJECT_POOL Objects;
    BOOT_U32 Ready, Reserved;
} X86_OBJECT_ARENA;
_Static_assert(sizeof(X86_OBJECT_ARENA) <= 2304, "Arena metadata bounded to 2304 bytes");
int x86_object_arena_attach(X86_OBJECT_ARENA *owner, X86_REGION_MAPPER *mapper, const X86_REGION_HANDLE *region);
int x86_object_arena_validate(const X86_OBJECT_ARENA *owner);
int x86_object_arena_allocate(X86_OBJECT_ARENA *owner, BOOT_U64 bytes, BOOT_U64 alignment, KERNEL_OBJECT_HANDLE *output);
unsigned char *x86_object_arena_data(const X86_OBJECT_ARENA *owner, const KERNEL_OBJECT_HANDLE *handle);
int x86_object_arena_release(X86_OBJECT_ARENA *owner, KERNEL_OBJECT_HANDLE *handle);
int x86_object_arena_detach(X86_OBJECT_ARENA *owner);
#endif
