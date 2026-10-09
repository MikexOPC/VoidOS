#include "handoff.h"
#include "handoff/resources.h"
#include "paging/preparation.h"
#include "paging/verification/contents.h"
#include "memory/ownership.h"
#include "lib/memory/address_ranges.h"
#include "acpi/discovery.h"
#include "graphics/handoff.h"

// ====== lo stato di uscita non puo modificare risorse o metadati presi in prestito ====== //

static int state_overlaps(const BOOT_EXIT_STATE *state, UINT64 base, UINT64 bytes)
{
    return os_address_ranges_overlap((UINT64)(UINTN)state, sizeof(*state), base, bytes);
}

static EFI_STATUS check_state_bindings(const BOOT_EXIT_HANDOFF *handoff, const BOOT_EXIT_STATE *state)
{
    UINT32 index;
    if (!handoff) return EFI_INVALID_PARAMETER;
    if (state_overlaps(state, (UINT64)(UINTN)handoff, sizeof(*handoff)) ||
        (handoff->Kernel && state_overlaps(state, (UINT64)(UINTN)handoff->Kernel, sizeof(*handoff->Kernel))) ||
        (handoff->Stack && state_overlaps(state, (UINT64)(UINTN)handoff->Stack, sizeof(*handoff->Stack))) ||
        (handoff->Capabilities && state_overlaps(state, (UINT64)(UINTN)handoff->Capabilities, sizeof(*handoff->Capabilities))) ||
        (handoff->Tables && state_overlaps(state, (UINT64)(UINTN)handoff->Tables, sizeof(*handoff->Tables))) ||
        (handoff->Page && state_overlaps(state, (UINT64)(UINTN)handoff->Page, sizeof(*handoff->Page))) ||
        (handoff->Memory && state_overlaps(state, (UINT64)(UINTN)handoff->Memory, sizeof(*handoff->Memory))))
        return EFI_INVALID_PARAMETER;
    if (handoff->Page && state_overlaps(state, handoff->Page->PhysicalBase, 4096)) return EFI_INVALID_PARAMETER;
    if (handoff->Descriptors) {
        const BOOT_DESCRIPTOR_STORAGE *owner = handoff->Descriptors;
        if (owner->Pages > BOOT_DESCRIPTOR_PAGES ||
            state_overlaps(state, (UINT64)(UINTN)owner, sizeof(*owner)) ||
            state_overlaps(state, owner->PhysicalBase, owner->Pages * 4096) ||
            state_overlaps(state, owner->CodeBase, owner->CodeBase ? 4096 : 0)) return EFI_INVALID_PARAMETER;
    }
    if (handoff->Memory && state_overlaps(state, (UINT64)(UINTN)handoff->Memory->Snapshot.Buffer,
                                         handoff->Memory->Snapshot.Capacity)) return EFI_INVALID_PARAMETER;
    if (handoff->Stack) {
        if (handoff->Stack->Pages > BOOT_STACK_PAGES) return EFI_INVALID_PARAMETER;
        if (state_overlaps(state, handoff->Stack->PhysicalBase, handoff->Stack->Pages * 4096)) return EFI_INVALID_PARAMETER;
    }
    if (handoff->Kernel) {
        if (handoff->Kernel->FileBufferBytes > ELF_MAX_FILE_BYTES ||
            state_overlaps(state, (UINT64)(UINTN)handoff->Kernel->FileBuffer,
                           handoff->Kernel->FileBufferBytes)) return EFI_INVALID_PARAMETER;
        if (handoff->Kernel->Pages.Count > ELF_MAX_LOAD_SEGMENTS) return EFI_INVALID_PARAMETER;
        for (index = 0; index < handoff->Kernel->Pages.Count; ++index) {
            const ELF_STAGED_SEGMENT *segment = &handoff->Kernel->Pages.Segments[index];
            if (segment->Pages > ELF_MAX_MEMORY_BYTES / 4096 ||
                state_overlaps(state, segment->PhysicalBase, segment->Pages * 4096)) return EFI_INVALID_PARAMETER;
        }
    }
    if (handoff->Tables) {
        if (handoff->Tables->Count > BOOT_TABLE_LIMIT) return EFI_INVALID_PARAMETER;
        for (index = 0; index < handoff->Tables->Count; ++index)
            if (state_overlaps(state, handoff->Tables->Pages[index], 4096)) return EFI_INVALID_PARAMETER;
    }
    return EFI_SUCCESS;
}

// ====== ricostruzione del piano senza allocazioni o modifiche alle tabelle ====== //

EFI_STATUS boot_check_exit_resources(const BOOT_EXIT_HANDOFF *handoff, BOOT_MAPPING_PLAN *plan)
{
    const BOOT_PAGING_CAPABILITIES *caps;
    BOOT_START_PAGE reserved;
    BOOT_MAPPING mapping;
    const BOOT_MEMORY_MAP *map;
    EFI_STATUS status;
    if (!handoff || !handoff->Capabilities || !handoff->Memory || !handoff->Page)
        return EFI_INVALID_PARAMETER;
    caps = handoff->Capabilities;
    map = &handoff->Memory->Snapshot;
    if (caps->Valid != 1 || caps->NxSupported != 1 || caps->PhysicalBits < 32 || caps->PhysicalBits > 52 ||
        !handoff->Memory->Pages || handoff->Memory->Pages > BOOT_MAP_MAX_BYTES / 4096 ||
        map->Capacity != handoff->Memory->Pages * 4096 || map->Valid != 1)
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    status = boot_plan_mappings(handoff->Kernel, handoff->Stack, plan);
    if (status != EFI_SUCCESS) return status;
    reserved = *handoff->Page;
    reserved.Ready = 0;
    status = boot_map_start_page(plan, &reserved);
    if (status != EFI_SUCCESS) return status;
    if (plan->Count == BOOT_MAPPING_LIMIT) return EFI_OUT_OF_RESOURCES;
    mapping.PhysicalBase = (UINT64)(UINTN)map->Buffer;
    mapping.VirtualBase = BOOT_MAP_VIRTUAL;
    mapping.Pages = handoff->Memory->Pages;
    mapping.Flags = 4;
    plan->Mappings[plan->Count] = plan->Mappings[plan->Count - 1];
    plan->Mappings[plan->Count - 1] = mapping;
    ++plan->Count;
    if (handoff->Descriptors) {
        status = boot_check_descriptor_storage(handoff->Descriptors);
        if (status == EFI_SUCCESS) status = boot_map_descriptor_storage(plan, handoff->Descriptors);
        if (status != EFI_SUCCESS) return status;
    }
    status = boot_verify_table_contents(plan, handoff->Tables, caps->PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_check_resources(map, handoff->Kernel, handoff->Stack);
    if (status == EFI_SUCCESS) status = boot_check_table_resources(map, handoff->Tables, caps->PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_check_loader_range(map, handoff->Page->PhysicalBase, 1);
    if (status == EFI_SUCCESS) status = boot_check_loader_range(map, mapping.PhysicalBase, mapping.Pages);
    if (status == EFI_SUCCESS && handoff->Descriptors)
        status = boot_check_loader_range(map, handoff->Descriptors->PhysicalBase, BOOT_DESCRIPTOR_PAGES);
    if (status == EFI_SUCCESS && handoff->Descriptors)
        status = boot_check_code_range(map, handoff->Descriptors->CodeBase);
    return status;
}

// ====== preflight della bozza completa prima di qualsiasi tentativo di uscita ====== //

EFI_STATUS boot_validate_exit_handoff(const BOOT_EXIT_HANDOFF *handoff)
{
    BOOT_MAPPING_PLAN plan;
    const BOOT_START_INFO *info;
    const BOOT_MEMORY_MAP *map;
    EFI_STATUS status;
    if (!handoff || !handoff->Page || handoff->Page->Ready != 1) return EFI_INVALID_PARAMETER;
    status = boot_check_exit_resources(handoff, &plan);
    if (status != EFI_SUCCESS) return status;
    info = (const BOOT_START_INFO *)(UINTN)handoff->Page->PhysicalBase;
    map = &handoff->Memory->Snapshot;
    if (info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION ||
        info->Size != sizeof(*info) || info->RequiredFeatures != BOOT_START_FEATURES || info->EnabledFeatures ||
        info->PageTableRoot != handoff->Tables->Root || info->StackBase != BOOT_STACK_VIRTUAL ||
        info->StackBytes != X86_BOOT_STACK_BYTES || info->KernelEntry != plan.Entry || info->Reserved ||
        info->MemoryMapVirtual != BOOT_MAP_VIRTUAL || info->MemoryMapCapacity != map->Capacity ||
        info->MemoryMapBytes != map->Size || info->MemoryDescriptorBytes != map->DescriptorSize ||
        info->MemoryDescriptorVersion != map->DescriptorVersion || info->MemoryReserved ||
        boot_validate_acpi_snapshot(info, map, handoff->Capabilities->PhysicalBits) != EFI_SUCCESS ||
        boot_validate_framebuffer_snapshot(info, map, handoff->Capabilities->PhysicalBits) != EFI_SUCCESS)
        return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}

// ====== verifica e pubblicazione della fotografia di ciascun tentativo ====== //

static EFI_STATUS check_snapshot(const BOOT_MEMORY_MAP *map, void *context)
{
    const BOOT_EXIT_HANDOFF *handoff = (const BOOT_EXIT_HANDOFF *)context;
    BOOT_MAPPING_PLAN plan;
    BOOT_START_INFO *info = (BOOT_START_INFO *)(UINTN)handoff->Page->PhysicalBase;
    OS_ACPI_ROOT acpi = info->Acpi;
    OS_FRAMEBUFFER framebuffer = info->Framebuffer;
    EFI_STATUS status;
    boot_invalidate_start_info(info);
    handoff->Page->Ready = 0;
    if (map != &handoff->Memory->Snapshot) return EFI_INVALID_PARAMETER;
    status = boot_check_exit_resources(handoff, &plan);
    if (status == EFI_SUCCESS)
        status = boot_prepare_start_info(&plan, handoff->Tables, handoff->Capabilities, info);
    if (status == EFI_SUCCESS) status = boot_attach_memory_snapshot(info, map);
    if (status == EFI_SUCCESS) {
        status = boot_attach_acpi_snapshot(info, map, &acpi, handoff->Capabilities->PhysicalBits);
    }
    if (status == EFI_SUCCESS)
        status = boot_attach_framebuffer_snapshot(info,map,&framebuffer,handoff->Capabilities->PhysicalBits);
    if (status == EFI_SUCCESS) handoff->Page->Ready = 1;
    else boot_invalidate_start_info(info);
    return status;
}

// ====== uscita terminale senza cleanup o servizi diagnostici ====== //

EFI_STATUS boot_exit_memory_handoff(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                   const BOOT_EXIT_HANDOFF *handoff, BOOT_EXIT_STATE *state)
{
    EFI_STATUS status;
    if (!state || state->Phase != BOOT_SERVICES_AVAILABLE || state->Attempts) return EFI_INVALID_PARAMETER;
    status = check_state_bindings(handoff, state);
    if (status != EFI_SUCCESS) return status;
    if (services && state_overlaps(state, (UINT64)(UINTN)services,
        services->Prefix.Hdr.HeaderSize < sizeof(*services) ? sizeof(*services) : services->Prefix.Hdr.HeaderSize))
        return EFI_INVALID_PARAMETER;
    status = boot_validate_exit_handoff(handoff);
    if (status != EFI_SUCCESS) { state->LastStatus = status; return status; }
    status = boot_exit_firmware_services_checked(image, services, &handoff->Memory->Snapshot, state,
                                                 check_snapshot, (void *)handoff);
    if (status != EFI_SUCCESS) {
        boot_invalidate_start_info((BOOT_START_INFO *)(UINTN)handoff->Page->PhysicalBase);
        handoff->Page->Ready = 0;
    }
    return status;
}
