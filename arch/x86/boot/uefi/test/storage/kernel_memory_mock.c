#include "kernel_memory_mock.h"

__declspec(align(4096)) static UINT8 pages[2][4096];
__declspec(align(4096)) static UINT8 stack_pages[64 * 1024];
__declspec(align(4096)) static UINT8 table_pages[64][4096];
UINTN kernel_mock_page_allocations, kernel_mock_page_frees;
int kernel_mock_scenario;
UINT64 kernel_mock_bad_address;
static UINTN sizing_calls;
static UINT64 final_buffer;
static UINTN table_count;
static UINT8 table_live[64];
static __declspec(align(4096)) UINT8 descriptor_pages[9 * 4096];
static int descriptor_live;

// ====== oracolo di proprieta separato dal conteggio dei tentativi ====== //

UINT32 kernel_mock_descriptor_live(void)
{
    return descriptor_live ? 1U : 0U;
}

// ====== pagine simulate per la preparazione ====== //

EFI_STATUS EFIAPI test_allocate_pages(UINT32 type, UINT32 memory, UINTN count, UINT64 *address)
{
    if (!kernel_mock_page_allocations) {
        UINTN index;
        table_count = 0;
        descriptor_live = 0;
        for (index = 0; index < 64; ++index) table_live[index] = 0;
    }
    if (!type && memory == 2 && count == 9) {
        if (kernel_mock_scenario == 60) return EFI_OUT_OF_RESOURCES;
        if (kernel_mock_scenario == 61) { *address = 0; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 62) { *address = (UINT64)pages[0]; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 63) { *address = (UINT64)descriptor_pages + 1; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 69) { *address = (1ULL << 47) - 4096; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 70) { *address = ~0ULL - 4095; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 71) { *address = kernel_mock_bad_address; return EFI_SUCCESS; }
        if (kernel_mock_scenario == 72) { *address = (UINT64)descriptor_pages; return EFI_SUCCESS; }
        if (descriptor_live) return EFI_OUT_OF_RESOURCES;
        *address = (UINT64)descriptor_pages;
        descriptor_live = 1;
        ++kernel_mock_page_allocations;
        return EFI_SUCCESS;
    }
    if (type == 0 && memory == 2 && count == 1 && kernel_mock_page_allocations >= 3 &&
        table_count < 64) {
        if ((kernel_mock_scenario == 46 && sizing_calls == 3) ||
            (kernel_mock_scenario == 32 && kernel_mock_page_allocations == 5) ||
            (kernel_mock_scenario == 40 && kernel_mock_page_allocations == 3))
            return EFI_OUT_OF_RESOURCES;
        if (kernel_mock_scenario == 65 && descriptor_live) return EFI_OUT_OF_RESOURCES;
        *address = (UINT64)table_pages[table_count];
        table_live[table_count++] = 1;
        ++kernel_mock_page_allocations;
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
    if (count == 9 && address == (UINT64)descriptor_pages) {
        if (kernel_mock_scenario == 68) return EFI_DEVICE_ERROR;
        if (!descriptor_live) return EFI_INVALID_PARAMETER;
        descriptor_live = 0;
        return EFI_SUCCESS;
    }
    if (count == 1 && address >= (UINT64)table_pages &&
        address < (UINT64)table_pages + sizeof(table_pages) && !(address & 4095)) {
        UINTN slot = (UINTN)((address - (UINT64)table_pages) / 4096);
        if (!table_live[slot]) return EFI_INVALID_PARAMETER;
        if ((kernel_mock_scenario == 44 && address == final_buffer) ||
            (kernel_mock_scenario == 35 && address == (UINT64)table_pages[1]) ||
            (kernel_mock_scenario == 41 && address == (UINT64)table_pages[0])) return EFI_DEVICE_ERROR;
        table_live[slot] = 0;
        return EFI_SUCCESS;
    }
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
    UINTN needed = 3 * 48, count = 0;
    UINT64 bases[69], sizes[69];
    UINT32 types[69];
    for (i = 0; i < 3; ++i) {
        bases[count] = i < 2 ? (UINT64)pages[i] : (UINT64)stack_pages;
        sizes[count] = i == 2 ? 16 : 1;
        types[count++] = kernel_mock_scenario == 30 && i == 0 ? 7 : 2;
    }
    for (i = 0; i < table_count; ++i) {
        if (!table_live[i]) continue;
        bases[count] = (UINT64)table_pages[i];
        sizes[count] = 1;
        types[count++] = kernel_mock_scenario == 34 && i == 0 ? 7 : 2;
    }
    if (descriptor_live) {
        bases[count] = (UINT64)descriptor_pages;
        sizes[count] = 9;
        types[count++] = kernel_mock_scenario == 67 ? 7 : 2;
    }
    if (kernel_mock_scenario == 72) {
        bases[count] = (UINT64)descriptor_pages;
        sizes[count] = 9;
        types[count++] = 1;
    }
    needed = count * 48;
    if (kernel_mock_page_allocations < 3 || table_count > 64)
        return EFI_LOAD_ERROR;
    if (kernel_mock_scenario == 64 && descriptor_live) return EFI_DEVICE_ERROR;
    if (kernel_mock_scenario == 26)
        return EFI_DEVICE_ERROR;
    if (kernel_mock_scenario == 36 && kernel_mock_page_allocations > 3)
        return EFI_DEVICE_ERROR;
    if (!buffer) {
        if (kernel_mock_page_allocations == 3) { sizing_calls = 0; final_buffer = 0; }
        ++sizing_calls;
    } else if (kernel_mock_page_allocations > 3 &&
               ((table_count && buffer == table_pages[table_count - 1]) || (UINT64)(UINTN)buffer == final_buffer)) {
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
    for (i = 0; i < count; ++i) {
        UINTN byte;
        UINT64 base = bases[i];
        bytes[i * 48] = (UINT8)types[i];
        bytes[i * 48 + 24] = (UINT8)sizes[i];
        for (byte = 0; byte < 8; ++byte)
            bytes[i * 48 + 8 + byte] = (UINT8)(base >> (byte * 8));
    }
    *size = needed;
    *key = 42;
    if (kernel_mock_scenario == 43 && kernel_mock_page_allocations > 4)
        ((UINT64 *)table_pages[1])[0] |= 4;
    return EFI_SUCCESS;
}
