#ifndef OS_KERNEL_MEMORY_MOCK_H
#define OS_KERNEL_MEMORY_MOCK_H
#include "file_protocol.h"

extern UINTN kernel_mock_page_allocations, kernel_mock_page_frees;
extern int kernel_mock_scenario;
extern UINT64 kernel_mock_bad_address;
UINT32 kernel_mock_descriptor_live(void);
EFI_STATUS EFIAPI test_allocate_pages(UINT32 type, UINT32 memory, UINTN count, UINT64 *address);
EFI_STATUS EFIAPI test_free_pages(UINT64 address, UINTN count);
EFI_STATUS EFIAPI test_get_memory_map(UINTN *size, void *buffer, UINTN *key, UINTN *stride, UINT32 *version);
#endif
