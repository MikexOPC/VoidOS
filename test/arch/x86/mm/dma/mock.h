#ifndef TEST_X86_DMA_ARENA_MOCK_H
#define TEST_X86_DMA_ARENA_MOCK_H
#include "arch/x86/mm/dma/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Real allocator and registry; CPU, PML4, window borrow and inventory capture are modeled.
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGE_WINDOW window;
extern X86_PAGING_INVENTORY inventory;
extern X86_DMA_ARENA arena;
extern BOOT_U64 roots[512];
extern unsigned checks, failure, borrows;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL DMA ARENA:%u\n", (unsigned)__LINE__); exit(1); } } while (0)

void arena_fixture(void);
int arena_capture(const KERNEL_BOOT_CONTEXT *boot, const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *out);
BOOT_U64 arena_frame_byte(BOOT_U64 physical);
#endif
