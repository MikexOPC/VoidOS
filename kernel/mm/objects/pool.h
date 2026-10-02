#ifndef OS_KERNEL_OBJECT_POOL_H
#define OS_KERNEL_OBJECT_POOL_H
#include "kernel/mm/inventory.h"
#define KERNEL_OBJECT_RECORDS 64U
#define KERNEL_OBJECT_MAX_BYTES 65536ULL
typedef struct { BOOT_U64 Offset, Bytes, Alignment, Id; } KERNEL_OBJECT_RECORD;
typedef struct { BOOT_U64 Offset, Bytes, Id, Owner; } KERNEL_OBJECT_HANDLE;
typedef struct {
    BOOT_U64 Self, Capacity, Used, NextId;
    unsigned char *Arena;
    BOOT_U32 Count, Ready;
    KERNEL_OBJECT_RECORD Records[KERNEL_OBJECT_RECORDS];
} KERNEL_OBJECT_POOL;
_Static_assert(sizeof(KERNEL_OBJECT_POOL) <= 3072, "Object metadata exceeds bootstrap budget");
int kernel_object_initialize(KERNEL_OBJECT_POOL *pool, void *arena, BOOT_U64 bytes);
int kernel_object_validate(const KERNEL_OBJECT_POOL *pool);
int kernel_object_allocate(KERNEL_OBJECT_POOL *pool, BOOT_U64 bytes, BOOT_U64 alignment, KERNEL_OBJECT_HANDLE *output);
unsigned char *kernel_object_data(const KERNEL_OBJECT_POOL *pool, const KERNEL_OBJECT_HANDLE *handle);
int kernel_object_release(KERNEL_OBJECT_POOL *pool, KERNEL_OBJECT_HANDLE *handle);
int kernel_object_detach(KERNEL_OBJECT_POOL *pool);
#endif
