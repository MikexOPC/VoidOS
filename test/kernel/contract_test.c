#include "kernel/boot/contract.h"
#include "kernel/mm/inventory.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
void mock_serial_mode(unsigned int mode);
unsigned int mock_serial_writes(void);
int mock_serial_colors(void);
static unsigned char map[144];

// ====== fixture con stride esteso e ordine fisico non richiesto ====== //

static void put(unsigned int offset, BOOT_U64 value, unsigned int width)
{
    unsigned int index;
    for (index = 0; index < width; ++index) { map[offset + index] = (unsigned char)value; value >>= 8; }
}

static void region(unsigned int index, BOOT_U32 type, BOOT_U64 base, BOOT_U64 pages, BOOT_U64 attributes)
{
    unsigned int offset = index * 48;
    put(offset, type, 4); put(offset + 8, base, 8); put(offset + 24, pages, 8); put(offset + 32, attributes, 8);
}

// ====== contratto invalido respinto e output non pubblicato ====== //

static int check_contract(void)
{
    BOOT_START_INFO info = {BOOT_START_SIGNATURE, BOOT_START_VERSION, sizeof(info), 3, 3,
        0x1000, X86_BOOT_STACK_VIRTUAL, X86_BOOT_STACK_BYTES, 0x100001000, 0,
        X86_BOOT_MAP_VIRTUAL, 144, 4096, 48, 1, 0};
    KERNEL_BOOT_CONTEXT output;
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(output); ++byte) ((volatile unsigned char *)&output)[byte] = 0;
    if (kernel_capture_contract(&info, 0x1000, info.KernelEntry, (KERNEL_BOOT_CONTEXT *)&info)) return 6;
    if (!kernel_capture_contract(&info, 0x1000, info.KernelEntry, &output) || output.Info.MemoryMapBytes != 144) return 1;
    for (byte = 0; byte < sizeof(info.Acpi); ++byte) {
        ((unsigned char *)&info.Acpi)[byte] = 1;
        if (kernel_capture_contract(&info, 0x1000, info.KernelEntry, &output) ||
            output.Info.Acpi.Source || output.Info.MemoryMapBytes != 144) return 7;
        ((unsigned char *)&info.Acpi)[byte] = 0;
    }
    info.Version = 2;
    if (kernel_validate_contract(&info, 0x1000, info.KernelEntry)) return 8;
    info.Version = BOOT_START_VERSION;
    info.Size = 208;
    if (kernel_validate_contract(&info,0x1000,info.KernelEntry)) return 12;
    info.Size = sizeof(info); info.Version = 4;
    if (kernel_validate_contract(&info,0x1000,info.KernelEntry)) return 13;
    info.Version = BOOT_START_VERSION;
    for (byte = 0; byte < sizeof(info.Gpu); ++byte) {
        ((unsigned char *)&info.Gpu)[byte] = 1;
        if (kernel_capture_contract(&info,0x1000,info.KernelEntry,&output) ||
            output.Info.Gpu.Present || output.Info.MemoryMapBytes != 144) return 14;
        ((unsigned char *)&info.Gpu)[byte] = 0;
    }
    for (byte = 0; byte < sizeof(info.Framebuffer); ++byte) {
        ((unsigned char *)&info.Framebuffer)[byte] = 1;
        if (kernel_capture_contract(&info,0x1000,info.KernelEntry,&output) ||
            output.Info.Framebuffer.Base || output.Info.MemoryMapBytes != 144) return 9;
        ((unsigned char *)&info.Framebuffer)[byte] = 0;
    }
    info.Framebuffer = (OS_FRAMEBUFFER){0x80000000,3145728,3145728,1024,768,1024,1};
    if (!kernel_capture_contract(&info,0x1000,info.KernelEntry,&output) ||
        output.Info.Framebuffer.Base != 0x80000000 || output.Info.Framebuffer.Stride != 1024) return 10;
    info.Framebuffer = (OS_FRAMEBUFFER){0};
    info.Version = 3;
    if (kernel_validate_contract(&info,0x1000,info.KernelEntry)) return 11;
    info.Version = BOOT_START_VERSION;
    info.MemoryMapBytes = 145;
    if (kernel_capture_contract(&info, 0x1000, info.KernelEntry, &output) || output.Info.MemoryMapBytes != 144) return 2;
    info.MemoryMapBytes = 144;
    if (kernel_validate_contract(&info, 0x2000, info.KernelEntry) ||
        kernel_validate_contract(&info, 0x1000, info.KernelEntry + 1)) return 3;
    info.EnabledFeatures = 0;
    if (kernel_validate_contract(&info, 0x1000, info.KernelEntry)) return 4;
    info.EnabledFeatures = 3;
    info.MemoryDescriptorBytes = 39;
    if (kernel_validate_contract(&info, 0x1000, info.KernelEntry)) return 5;
    return 0;
}

// ====== inventario senza alias output overflow o reclamazione loader ====== //

static int check_memory(void)
{
    KERNEL_MEMORY_INVENTORY output = {77, 88, 99};
    region(0, 7, 0x5000, 3, 8);
    region(1, 2, 0x1000, 4, 8);
    region(2, 7, 0x8000, 2, 1ULL << 63);
    if (!kernel_inventory_memory(map, 144, 48, &output) || output.ConventionalPages != 3 ||
        output.ReservedPages != 6 || output.Regions != 3) return 1;
    region(2, 7, 0x7000, 2, 0);
    if (kernel_inventory_memory(map, 144, 48, &output) || output.ConventionalPages != 3) return 2;
    region(2, 7, 0x8000, ~0ULL, 0);
    if (kernel_inventory_memory(map, 144, 48, &output)) return 3;
    region(2, 7, (1ULL << 52) - 4096, 2, 0);
    if (kernel_inventory_memory(map, 144, 48, &output)) return 4;
    region(2, 7, 0x8001, 2, 0);
    if (kernel_inventory_memory(map, 144, 48, &output)) return 5;
    region(2, 7, 0x8000, 0, 0);
    if (kernel_inventory_memory(map, 144, 48, &output)) return 6;
    region(2, 7, 0, 1, 0);
    if (!kernel_inventory_memory(map, 144, 48, &output) || output.ReservedPages != 5) return 7;
    // B8: a WB+XP ConventionalMemory region at 0 reserves only page zero.
    region(1, 2, 0x9000, 4, 8);
    region(2, 7, 0, 4, 0x400f);
    if (!kernel_inventory_memory(map, 144, 48, &output) || output.ConventionalPages != 6 ||
        output.ReservedPages != 5) return 9;
    region(1, 2, 0x1000, 4, 8);
    region(2, 7, 0, 1, 0);
    if (kernel_inventory_memory(map, 143, 48, &output) || kernel_inventory_memory(map, 144, 39, &output) ||
        kernel_inventory_memory(map, 144, 48, (KERNEL_MEMORY_INVENTORY *)map)) return 8;
    return 0;
}

// ====== diagnostica normale assente e timeout senza attese infinite ====== //

__declspec(dllexport) int run_kernel_tests(void)
{
    int result = check_contract();
    if (result) return 100 + result;
    result = check_memory();
    if (result) return 200 + result;
    mock_serial_mode(0);
    if (!x86_serial_initialize() || !kernel_section("test") || !kernel_log(0, "successo") ||
        !kernel_log(3, "errore") || !mock_serial_colors()) return 301;
    mock_serial_mode(1);
    if (x86_serial_initialize() || x86_serial_character('a') || mock_serial_writes()) return 302;
    mock_serial_mode(2);
    if (!x86_serial_initialize() || x86_serial_character('a')) return 303;
    if (kernel_text("ripetizione") || kernel_log(4, "livello invalido")) return 304;
    return 0;
}
