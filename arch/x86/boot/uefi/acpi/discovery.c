#include "discovery.h"
#include "console/log.h"
#include "lib/memory/address_ranges.h"
#include "memory/ownership.h"
static const EFI_GUID modern_guid = {0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0,0x80,0xc7,0x3c,0x88,0x81}};
static const EFI_GUID legacy_guid = {0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0,0x90,0x27,0x3f,0xc1,0x4d}};

// ====== copertura completa e unica prima di leggere memoria firmware ===== //

static int readable_range(const BOOT_MEMORY_MAP *map, UINT64 base, UINT64 bytes, UINT32 bits, int acpi)
{
    UINTN index;
    UINT32 found = 0;
    UINT64 limit;
    if (bits < 32 || bits > 52 || base < 4096 || !bytes) return 0;
    limit = 1ULL << bits;
    if (base >= (1ULL << 47) || base >= limit || bytes > limit - base || bytes > (1ULL << 47) - base) return 0;
    for (index = 0; index < map->Size / map->DescriptorSize; ++index) {
        BOOT_MEMORY_REGION region;
        UINT64 span;
        if (boot_read_memory_region(map, index, &region) != EFI_SUCCESS) return 0;
        span = region.Pages * 4096;
        if (!os_address_ranges_overlap(base, bytes, region.PhysicalBase, span)) continue;
        if (acpi ? (region.FirmwareType != 9 && region.FirmwareType != 10) :
            (!region.FirmwareType || region.FirmwareType > 10 || region.FirmwareType == 8)) return 0;
        if (!(region.Attributes & 8) || (region.Attributes & 0x2000) ||
            base < region.PhysicalBase || base - region.PhysicalBase >= span ||
            bytes > span - (base - region.PhysicalBase)) return 0;
        ++found;
    }
    return found == 1;
}

// ====== confronto GUID bounded senza strcmp o formati packed ===== //

static int same_guid(const EFI_GUID *first, const EFI_GUID *second)
{
    UINT32 index;
    for (index = 0; index < sizeof(*first); ++index)
        if (((const UINT8 *)first)[index] != ((const UINT8 *)second)[index]) return 0;
    return 1;
}

// ====== alias di output respinti prima di modificare qualsiasi byte ===== //

static int output_alias(const OS_ACPI_ROOT *output, UINT64 base, UINT64 bytes)
{
    return os_address_ranges_overlap((UINT64)(UINTN)output, sizeof(*output), base, bytes);
}

// ====== preferenza ACPI 2 senza fallback dopo una voce moderna malformata ===== //

static EFI_STATUS select_root(const EFI_SYSTEM_TABLE *system, const BOOT_MEMORY_MAP *map,
                               UINT32 bits, const OS_ACPI_ROOT *output, UINT64 *physical, UINT32 *source)
{
    const BOOT_CONFIGURATION_TABLE *entries = system->ConfigurationTable;
    UINTN index, count = system->NumberOfTableEntries;
    UINT64 modern = 0, legacy = 0;
    UINT32 modern_count = 0, legacy_count = 0;
    if (count > 4096) return EFI_INVALID_PARAMETER;
    if (!count) return EFI_NOT_FOUND;
    if ((UINT64)(UINTN)entries & 7) return EFI_LOAD_ERROR;
    if (output_alias(output, (UINT64)(UINTN)entries, count * sizeof(*entries))) return EFI_INVALID_PARAMETER;
    if (!readable_range(map, (UINT64)(UINTN)entries, count * sizeof(*entries), bits, 0))
        return EFI_LOAD_ERROR;
    for (index = 0; index < count; ++index) {
        if (same_guid(&entries[index].VendorGuid, &modern_guid)) {
            modern = (UINT64)(UINTN)entries[index].VendorTable; ++modern_count;
        } else if (same_guid(&entries[index].VendorGuid, &legacy_guid)) {
            legacy = (UINT64)(UINTN)entries[index].VendorTable; ++legacy_count;
        }
    }
    if (modern_count > 1 || (!modern_count && legacy_count > 1)) return EFI_LOAD_ERROR;
    if (!modern_count && !legacy_count) return EFI_NOT_FOUND;
    *physical = modern_count ? modern : legacy;
    *source = modern_count ? 2 : 1;
    if (output_alias(output, *physical, 36)) return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}

// ====== copia in due passaggi senza leggere campi oltre il formato legacy ===== //

static EFI_STATUS copy_root(const BOOT_MEMORY_MAP *map, UINT32 bits, OS_ACPI_ROOT *draft)
{
    const volatile UINT8 *bytes = (const volatile UINT8 *)(UINTN)draft->Physical;
    UINT32 index, length = 0;
    if (!readable_range(map, draft->Physical, 20, bits, 1)) return EFI_LOAD_ERROR;
    for (index = 0; index < 20; ++index) draft->Bytes[index] = bytes[index];
    if (draft->Bytes[15] == 0) draft->Length = 20;
    else {
        if (draft->Bytes[15] == 1) return EFI_UNSUPPORTED;
        if (!readable_range(map, draft->Physical, 36, bits, 1)) return EFI_LOAD_ERROR;
        for (index = 20; index < 36; ++index) draft->Bytes[index] = bytes[index];
        for (index = 0; index < 4; ++index) length |= (UINT32)draft->Bytes[20 + index] << (8 * index);
        if (length != 36) return EFI_UNSUPPORTED;
        draft->Length = length;
    }
    return os_acpi_root_valid(draft, bits) ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

// ====== discovery bounded con pubblicazione solo dopo verifica completa ===== //

EFI_STATUS boot_discover_acpi(const EFI_SYSTEM_TABLE *system, const BOOT_MEMORY_MAP *map,
                              UINT32 bits, OS_ACPI_ROOT *output)
{
    OS_ACPI_ROOT draft = {0};
    EFI_STATUS status;
    if (!system || !map || !output || ((UINT64)(UINTN)output & 7) || bits < 32 || bits > 52 ||
        (UINT64)(UINTN)output > ~0ULL - sizeof(*output) ||
        system->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE || system->Hdr.HeaderSize < sizeof(*system) ||
        system->NumberOfTableEntries > 4096 ||
        output_alias(output, (UINT64)(UINTN)system, sizeof(*system)) ||
        output_alias(output, (UINT64)(UINTN)map, sizeof(*map)) ||
        output_alias(output, (UINT64)(UINTN)map->Buffer, map->Capacity) ||
        output_alias(output, (UINT64)(UINTN)system->ConfigurationTable,
                        system->NumberOfTableEntries * sizeof(BOOT_CONFIGURATION_TABLE))) return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    status = select_root(system, map, bits, output, &draft.Physical, &draft.Source);
    if (status == EFI_SUCCESS) status = copy_root(map, bits, &draft);
    if (status == EFI_SUCCESS || status == EFI_NOT_FOUND) *output = draft;
    return status;
}

// ====== verifica del prestito fisico anche sulle fotografie finali di uscita ===== //

EFI_STATUS boot_validate_acpi_snapshot(const BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map, UINT32 bits)
{
    EFI_STATUS status;
    if (!info || !map || !os_acpi_root_valid(&info->Acpi, bits)) return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    if (!info->Acpi.Source) return EFI_SUCCESS;
    return readable_range(map, info->Acpi.Physical, info->Acpi.Length, bits, 1) ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

// ====== snapshot nella pagina R NX gia allocata senza nuove risorse ===== //

EFI_STATUS boot_attach_acpi_snapshot(BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map,
                                     const OS_ACPI_ROOT *root, UINT32 bits)
{
    OS_ACPI_ROOT previous;
    EFI_STATUS status;
    if (!info || !map || !root || !os_acpi_root_valid(root, bits) ||
        info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION ||
        info->Size != sizeof(*info) || info->RequiredFeatures != BOOT_START_FEATURES || info->EnabledFeatures ||
        (UINT64)(UINTN)info > ~0ULL - sizeof(*info) ||
        os_address_ranges_overlap((UINT64)(UINTN)info, sizeof(*info), (UINT64)(UINTN)root, sizeof(*root)) ||
        os_address_ranges_overlap((UINT64)(UINTN)info, sizeof(*info), (UINT64)(UINTN)map, sizeof(*map)) ||
        os_address_ranges_overlap((UINT64)(UINTN)info, sizeof(*info), (UINT64)(UINTN)map->Buffer, map->Capacity))
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    previous = info->Acpi;
    info->Acpi = *root;
    status = boot_validate_acpi_snapshot(info, map, bits);
    if (status != EFI_SUCCESS) info->Acpi = previous;
    return status;
}

// ====== discovery e pubblicazione prima dei log di successo ===== //

EFI_STATUS boot_prepare_acpi_handoff(EFI_SYSTEM_TABLE *system, const BOOT_MEMORY_MAP *map,
                                     UINT32 bits, const BOOT_START_PAGE *page)
{
    OS_ACPI_ROOT root;
    BOOT_START_INFO *info;
    EFI_STATUS status;
    if (!system || !map || !page || page->Ready != 1 || page->Pages != 1 || !page->PhysicalBase ||
        bits < 32 || bits > 52 || page->PhysicalBase >= (1ULL << bits) ||
        page->PhysicalBase >= (1ULL << 47) || (page->PhysicalBase & 4095)) return EFI_INVALID_PARAMETER;
    if (boot_check_loader_range(map, page->PhysicalBase, 1) != EFI_SUCCESS) return EFI_LOAD_ERROR;
    if (os_address_ranges_overlap(page->PhysicalBase, 4096, (UINT64)(UINTN)system, sizeof(*system)) ||
        os_address_ranges_overlap(page->PhysicalBase, 4096, (UINT64)(UINTN)page, sizeof(*page)) ||
        os_address_ranges_overlap(page->PhysicalBase, 4096, (UINT64)(UINTN)map, sizeof(*map)) ||
        os_address_ranges_overlap(page->PhysicalBase, 4096, (UINT64)(UINTN)map->Buffer, map->Capacity) ||
        system->NumberOfTableEntries > 4096 ||
        os_address_ranges_overlap(page->PhysicalBase, 4096, (UINT64)(UINTN)system->ConfigurationTable,
                                  system->NumberOfTableEntries * sizeof(BOOT_CONFIGURATION_TABLE))) return EFI_INVALID_PARAMETER;
    info = (BOOT_START_INFO *)(UINTN)page->PhysicalBase;
    if (info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION || info->Size != sizeof(*info))
        return EFI_INVALID_PARAMETER;
    status = boot_section(system->ConOut, L"discovery ACPI RSDP");
    if (status != EFI_SUCCESS) return status;
    status = boot_discover_acpi(system, map, bits, &root);
    if (status == EFI_NOT_FOUND) {
        status = boot_attach_acpi_snapshot(info, map, &root, bits);
        if (status != EFI_SUCCESS) return status;
        return boot_log(system->ConOut, BOOT_WARN, L"RSDP assente; discovery dispositivi ACPI non disponibile.");
    }
    if (status != EFI_SUCCESS) {
        boot_log(system->ConOut, BOOT_FAIL, L"RSDP o risorse firmware non validi.");
        return status;
    }
    status = boot_attach_acpi_snapshot(info, map, &root, bits);
    if (status != EFI_SUCCESS) return status;
    status = boot_log_hex(system->ConOut, L"ACPI_RSDP_PHYSICAL: ", root.Physical);
    if (status == EFI_SUCCESS) status = boot_log_hex(system->ConOut, L"ACPI_ROOT_PHYSICAL: ", os_acpi_root_table(&root, bits));
    if (status == EFI_SUCCESS) status = boot_log(system->ConOut, BOOT_OK, L"RSDP copiato: GUID, copertura e checksum verificati.");
    return status;
}
