#ifndef OS_TEST_PAGING_FIXTURE_H
#define OS_TEST_PAGING_FIXTURE_H
#include "arch/x86/mm/paging/inventory.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/kernel/drivers/video/framebuffer/mapping.h"
extern BOOT_U64 tables[129][512], views[129];
extern BOOT_U32 table_count, reads, missing;
extern unsigned char map[144];
extern KERNEL_BOOT_CONTEXT context;
extern KERNEL_PHYSICAL_ALLOCATOR pool;
extern X86_PAGING_INVENTORY ledger, saved;
extern X86_LAPIC_MAPPING mapping;
extern BOOT_U32 authority_bad, authority_calls, authority_failure;
extern X86_PAGING_VIDEO_AUTH video;
extern BOOT_U32 video_calls, video_failure;
void clear(void *object, BOOT_U64 bytes);
void number(unsigned char *buffer, BOOT_U64 value);
void save_ledger(void);
int fixture(void);
int video_owned(void);
int video_corruption(void);
int dma_arena(void);
int video_arena(void);
#endif
