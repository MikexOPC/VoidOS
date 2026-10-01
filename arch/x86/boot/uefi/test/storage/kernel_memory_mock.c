#include "kernel_memory_mock.h"

__declspec(align(4096)) static UINT8 pages[2][4096];
__declspec(align(4096)) static UINT8 stack_pages[64 * 1024];
__declspec(align(4096)) static UINT8 table_pages[16][4096];
UINTN kernel_mock_page_allocations, kernel_mock_page_frees;
int kernel_mock_scenario;
static UINTN sizing_calls;
static UINT64 final_buffer;

// ====== pagine simulate per la preparazione ====== //

EFI_STATUS EFIAPI test_allocate_pages(UINT32 type, UINT32 memory, UINTN count, UINT64 *address)
{
    if (type == 0 && memory == 2 && count == 1 && kernel_mock_page_allocations >= 3 &&
        kernel_mock_page_allocations < 19) {
        if ((kernel_mock_scenario == 46 && sizing_calls == 3) ||
            (kernel_mock_scenario == 32 && kernel_mock_page_allocations == 5) ||
            (kernel_mock_scenario == 40 && kernel_mock_page_allocations == 3))
            return EFI_OUT_OF_RESOURCES;
        *address = (UINT64)table_pages[kernel_mock_page_allocations++ - 3];
        return EFI_SUCCESS;
    }
    if (type == 0 && memory == 2 && count == 16 && kernel_mock_page_allocations == 2) {
        if (kernel_mock_scenario == 29) return EFI_OUT_OF_RESOURCES;
        *address = (UINT64)stack_pages;
        ++kernel_mock_page_allocations;
        return EFI_SUCCESS;
    }
    if (kernel_mock_scenario == 25 && kernel_mock_page_allocations == 1)
        return EFI_OUT_OF_RESOURCES;
    if (type || memory != 2 || count != 1 || kernel_mock_page_allocations >= 2)
        return EFI_OUT_OF_RESOURCES;
    *address = (UINT64)pages[kernel_mock_page_allocations++];
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI test_free_pages(UINT64 address, UINTN count)
{
    ++kernel_mock_page_frees;
    if (count == 1 && address >= (UINT64)table_pages &&
        address < (UINT64)table_pages + sizeof(table_pages) && !(address & 4095))
        return (kernel_mock_scenario == 44 && address == final_buffer) ||
            (kernel_mock_scenario == 35 && address == (UINT64)table_pages[1]) ||
            (kernel_mock_scenario == 41 && address == (UINT64)table_pages[0]) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    if (address == (UINT64)stack_pages && count == 16)
        return kernel_mock_scenario == 31 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    if ((address != (UINT64)pages[0] && address != (UINT64)pages[1]) || count != 1)
        return EFI_INVALID_PARAMETER;
    if (kernel_mock_scenario == 27 && address == (UINT64)pages[1])
        return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}
// ====== kernel_mock_scenario iniziale ====== //

EFI_STATUS EFIAPI test_get_memory_map(UINTN *size, void *buffer, UINTN *key,
                                       UINTN *stride, UINT32 *version)
{
    UINTN i;
    UINT8 *bytes = (UINT8 *)buffer;
    UINTN needed = kernel_mock_page_allocations * 48;
    if (kernel_mock_page_allocations < 3 || kernel_mock_page_allocations > 19 || kernel_mock_page_frees)
        return EFI_LOAD_ERROR;
    if (kernel_mock_scenario == 26)
        return EFI_DEVICE_ERROR;
    if (kernel_mock_scenario == 36 && kernel_mock_page_allocations > 3)
        return EFI_DEVICE_ERROR;
    if (!buffer) {
        if (kernel_mock_page_allocations == 3) { sizing_calls = 0; final_buffer = 0; }
        ++sizing_calls;
    } else if (kernel_mock_page_allocations > 3 &&
               buffer == table_pages[kernel_mock_page_allocations - 4]) {
        final_buffer = (UINT64)(UINTN)buffer;
        if (kernel_mock_scenario == 45) return EFI_DEVICE_ERROR;
    }
    *stride = 48;
    *version = 1;
    if (*size < needed) {
        *size = needed;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!buffer)
        return EFI_INVALID_PARAMETER;
    for (i = 0; i < needed; ++i)
        bytes[i] = 0;
    for (i = 0; i < kernel_mock_page_allocations; ++i) {
        UINTN byte;
        UINT64 base = i < 2 ? (UINT64)pages[i] : i == 2 ? (UINT64)stack_pages : (UINT64)table_pages[i - 3];
        bytes[i * 48] = (kernel_mock_scenario == 30 && i == 0) ||
            (kernel_mock_scenario == 34 && i == 3) ? 7 : 2;
        bytes[i * 48 + 24] = i == 2 ? 16 : 1;
        for (byte = 0; byte < 8; ++byte)
            bytes[i * 48 + 8 + byte] = (UINT8)(base >> (byte * 8));
    }
    *size = needed;
    *key = 42;
    if (kernel_mock_scenario == 43 && kernel_mock_page_allocations > 4)
        ((UINT64 *)table_pages[1])[0] |= 4;
    return EFI_SUCCESS;
}


