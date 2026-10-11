#include "acpi/discovery.h"
_Alignas(4096) static UINT8 region[8192];
_Alignas(4096) static UINT8 start_buffer[4096];
static UINT8 descriptors[96];
static EFI_SYSTEM_TABLE system;
static BOOT_MEMORY_MAP map;
static OS_ACPI_ROOT output, saved;
static const EFI_GUID modern = {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
static const EFI_GUID legacy = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a,0x16,0,0x90,0x27,0x3f,0xc1,0x4d}};

// ====== azzeramento bounded indipendente dal runtime C ===== //

static void clear(void *buffer, UINTN bytes)
{
    UINTN index;
    for (index = 0; index < bytes; ++index) ((volatile UINT8 *)buffer)[index] = 0;
}

// ====== scrittura di campi little endian senza strutture packed ===== //

static void number(UINT8 *buffer, UINT64 value, UINT32 width)
{
    UINT32 index;
    for (index = 0; index < width; ++index) buffer[index] = (UINT8)(value >> (index * 8));
}

// ====== checksum costruiti da una formula host indipendente ===== //

static void checksums(UINT8 *rsdp)
{
    UINT32 index, sum = 0;
    rsdp[8] = 0; rsdp[32] = 0;
    for (index = 0; index < 20; ++index) sum += rsdp[index];
    rsdp[8] = (UINT8)(0U - sum);
    sum = 0;
    for (index = 0; index < 36; ++index) sum += rsdp[index];
    rsdp[32] = (UINT8)(0U - sum);
}

// ====== due GUID firmware con preferenza moderna anche se arriva per ultima ===== //

static void fixture(void)
{
    BOOT_CONFIGURATION_TABLE *entries = (BOOT_CONFIGURATION_TABLE *)(void *)region;
    UINT8 *rsdp = region + 256;
    clear(region, sizeof(region)); clear(descriptors, sizeof(descriptors));
    clear(&system, sizeof(system)); clear(&map, sizeof(map)); clear(&output, sizeof(output));
    system.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE; system.Hdr.HeaderSize = sizeof(system);
    system.NumberOfTableEntries = 2; system.ConfigurationTable = entries;
    entries[0].VendorGuid = legacy; entries[0].VendorTable = rsdp + 64;
    entries[1].VendorGuid = modern; entries[1].VendorTable = rsdp;
    number(rsdp, 0x2052545020445352ULL, 8); rsdp[15] = 2;
    number(rsdp + 20, 36, 4); number(rsdp + 24, 0x200000, 8); checksums(rsdp);
    number(rsdp + 64, 0x2052545020445352ULL, 8);
    number(rsdp + 80, 0x300000, 4); checksums(rsdp + 64);
    descriptors[0] = 9; number(descriptors + 8, (UINT64)(UINTN)region, 8);
    descriptors[24] = 2; descriptors[32] = 8;
    map.Buffer = descriptors; map.Capacity = map.Size = 48;
    map.DescriptorSize = 48; map.DescriptorVersion = map.Valid = 1;
}

// ====== successo moderno fallback legacy e assenza esplicitamente rappresentata ===== //

static int selection(void)
{
    UINT32 index;
    fixture();
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_SUCCESS ||
        output.Physical != (UINT64)(UINTN)(region + 256) || output.Length != 36 || output.Source != 2 ||
        !os_acpi_root_valid(&output, 52) || os_acpi_root_table(&output, 52) != 0x200000) return 1;
    system.NumberOfTableEntries = 1;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_SUCCESS || output.Length != 20 ||
        output.Source != 1 || os_acpi_root_table(&output, 52) != 0x300000) return 2;
    for (index = 20; index < 36; ++index) if (output.Bytes[index]) return 3;
    system.NumberOfTableEntries = 0; system.ConfigurationTable = 0;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_NOT_FOUND) return 4;
    for (index = 0; index < sizeof(output); ++index) if (((UINT8 *)&output)[index]) return 5;
    if (!os_acpi_root_valid(&output, 52)) return 6;
    return 0;
}

// ====== modern corrotto non consente il ripiego silenzioso sul legacy ===== //

static int mutations(void)
{
    UINT32 bit, index;
    for (bit = 0; bit < 36 * 8; ++bit) {
        fixture(); region[256 + bit / 8] ^= (UINT8)(1U << (bit % 8));
        if (boot_discover_acpi(&system, &map, 52, &output) == EFI_SUCCESS) return 1;
        for (index = 0; index < sizeof(output); ++index) if (((UINT8 *)&output)[index]) return 2;
    }
    fixture(); region[256 + 15] = 1; checksums(region + 256);
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_UNSUPPORTED) return 3;
    fixture(); number(region + 256 + 20, 4096, 4); checksums(region + 256);
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_UNSUPPORTED) return 4;
    fixture(); ((BOOT_CONFIGURATION_TABLE *)(void *)region)[0].VendorGuid = modern;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 5;
    fixture(); ++region[256 + 8]; --region[256 + 32];
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 6;
    fixture(); region[256 + 33] = 1; checksums(region + 256);
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_SUCCESS) return 7;
    return 0;
}

// ====== puntatori non coperti vengono respinti prima della dereferenziazione ===== //

static int bounds(void)
{
    BOOT_CONFIGURATION_TABLE *entries = (BOOT_CONFIGURATION_TABLE *)(void *)region;
    UINT32 index;
    fixture(); system.ConfigurationTable = (void *)4096;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 1;
    fixture(); entries[1].VendorTable = (void *)4096;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 2;
    fixture(); system.NumberOfTableEntries = 4097;
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_INVALID_PARAMETER) return 3;
    fixture(); system.ConfigurationTable = (void *)(~0ULL - 15);
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 4;
    for (index = 0; index < 15; ++index) {
        fixture(); descriptors[0] = (UINT8)index;
        if (index == 9 || index == 10) continue;
        if (boot_discover_acpi(&system, &map, 52, &output) == EFI_SUCCESS) return 5;
    }
    fixture(); number(descriptors + 32, 0x2008, 8);
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 6;
    fixture(); entries[1].VendorTable = region + 8192 - 20;
    for (index = 0; index < 20; ++index) region[8192 - 20 + index] = region[256 + index];
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_LOAD_ERROR) return 7;
    return 0;
}

// ====== output disgiunto da tabella lista e fotografia memoria ===== //

static int aliases(void)
{
    fixture();
    if (boot_discover_acpi(&system, &map, 52, (OS_ACPI_ROOT *)(void *)region) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, &map, 52, (OS_ACPI_ROOT *)(void *)(region + 256)) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, &map, 52, (OS_ACPI_ROOT *)(void *)descriptors) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, &map, 52, (OS_ACPI_ROOT *)(void *)&system) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, &map, 52, (OS_ACPI_ROOT *)(void *)&map) != EFI_INVALID_PARAMETER) return 1;
    if (boot_discover_acpi(0, &map, 52, &output) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, 0, 52, &output) != EFI_INVALID_PARAMETER ||
        boot_discover_acpi(&system, &map, 52, 0) != EFI_INVALID_PARAMETER) return 2;
    return 0;
}

// ====== durata di vita indipendente dalla memoria originale del firmware ===== //

static int lifetime(void)
{
    UINT32 index;
    fixture();
    if (boot_discover_acpi(&system, &map, 52, &output) != EFI_SUCCESS) return 1;
    saved = output; clear(region + 256, 36);
    if (!os_acpi_root_valid(&output, 52)) return 2;
    for (index = 0; index < sizeof(output) * 8; ++index) {
        ((UINT8 *)&output)[index / 8] ^= (UINT8)(1U << (index % 8));
        if (index >= 128 && index < 416 && os_acpi_root_valid(&output, 52)) return 3;
        ((UINT8 *)&output)[index / 8] ^= (UINT8)(1U << (index % 8));
    }
    return os_acpi_root_table(&saved, 52) == 0x200000 ? 0 : 4;
}

// ====== console simulata per controllare il commit del contratto ===== //

static EFI_STATUS console_status;
static EFI_STATUS EFIAPI console_output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
{
    return self && text ? console_status : EFI_INVALID_PARAMETER;
}

// ====== consegna RSDP nella pagina posseduta e ricontrollo sulla mappa finale ===== //

static int handoff(void)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console = {0};
    BOOT_START_PAGE page = {(UINT64)(UINTN)start_buffer, 1, 1};
    BOOT_START_INFO *info = (BOOT_START_INFO *)(void *)start_buffer;
    fixture(); clear(start_buffer, sizeof(start_buffer));
    console.OutputString = console_output; console_status = EFI_SUCCESS; system.ConOut = &console;
    descriptors[48] = 2; number(descriptors + 56, page.PhysicalBase, 8);
    descriptors[72] = 1; descriptors[80] = 8; map.Size = map.Capacity = 96;
    info->Signature = BOOT_START_SIGNATURE; info->Version = BOOT_START_VERSION; info->Size = sizeof(*info);
    info->RequiredFeatures = BOOT_START_FEATURES;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_SUCCESS ||
        !os_acpi_root_valid(&info->Acpi, 52) || info->Acpi.Source != 2 ||
        boot_validate_acpi_snapshot(info, &map, 52) != EFI_SUCCESS) return 1;
    descriptors[0] = 7;
    if (boot_validate_acpi_snapshot(info, &map, 52) != EFI_LOAD_ERROR) return 2;
    clear(&saved, sizeof(saved));
    if (boot_attach_acpi_snapshot(info, &map, &saved, 52) != EFI_SUCCESS || info->Acpi.Source) return 8;
    saved.Source = 2; saved.Length = 36; saved.Physical = (UINT64)(UINTN)(region + 256);
    {
        UINT32 byte;
        for (byte = 0; byte < 36; ++byte) saved.Bytes[byte] = region[256 + byte];
    }
    if (boot_attach_acpi_snapshot(info, &map, &saved, 52) != EFI_LOAD_ERROR || info->Acpi.Source) return 9;
    if (boot_attach_acpi_snapshot(info, &map, &info->Acpi, 52) != EFI_INVALID_PARAMETER) return 10;
    descriptors[0] = 9;
    if (boot_attach_acpi_snapshot(info, &map, &saved, 52) != EFI_SUCCESS) return 11;
    region[256] ^= 1;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_LOAD_ERROR ||
        !os_acpi_root_valid(&info->Acpi, 52)) return 3;
    region[256] ^= 1; console_status = EFI_DEVICE_ERROR;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_DEVICE_ERROR) return 4;
    console_status = EFI_SUCCESS; system.NumberOfTableEntries = 0;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_SUCCESS || info->Acpi.Source ||
        !os_acpi_root_valid(&info->Acpi, 52) || boot_validate_acpi_snapshot(info, &map, 52) != EFI_SUCCESS) return 5;
    system.ConfigurationTable = start_buffer; system.NumberOfTableEntries = 1;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_INVALID_PARAMETER) return 6;
    page.PhysicalBase = 4096;
    if (boot_prepare_acpi_handoff(&system, &map, 52, &page) != EFI_LOAD_ERROR) return 7;
    return 0;
}

// ====== suite export senza privilegi o hardware reale ===== //

__declspec(dllexport) int run_acpi_discovery_tests(void)
{
    int result = selection(); if (result) return 100 + result;
    result = mutations(); if (result) return 200 + result;
    result = bounds(); if (result) return 300 + result;
    result = aliases(); if (result) return 400 + result;
    result = lifetime(); if (result) return 500 + result;
    result = handoff(); if (result) return 600 + result;
    return 0;
}
