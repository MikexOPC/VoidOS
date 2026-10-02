#include "storage.h"
#include "handoff/firmware/terminal/handoff.h"
#include "paging/preparation.h"
#include "paging/verification/contents.h"
#include "memory/ownership.h"

// ====== ricostruzione del grafo inattivo con tutte le nuove pagine protette ====== //

EFI_STATUS boot_prepare_descriptor_handoff(EFI_SYSTEM_TABLE *system_table, BOOT_EXIT_HANDOFF *handoff,
                                           BOOT_DESCRIPTOR_STORAGE *owner, BOOT_PAGE_TABLES *tables)
{
    EFI_BOOT_SERVICES_PREFIX *services;
    BOOT_MAPPING_PLAN plan;
    BOOT_START_INFO *info;
    EFI_STATUS status;
    if (!system_table || !handoff || handoff->Descriptors || !tables || handoff->Tables != tables)
        return EFI_INVALID_PARAMETER;
    status = boot_check_descriptor_storage(owner);
    if (status == EFI_SUCCESS) status = boot_check_exit_resources(handoff, &plan);
    if (status == EFI_SUCCESS) status = boot_map_descriptor_storage(&plan, owner);
    if (status != EFI_SUCCESS) return status;
    services = (EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->AllocatePages || !services->FreePages || !services->GetMemoryMap) return EFI_UNSUPPORTED;
    info = (BOOT_START_INFO *)(UINTN)handoff->Page->PhysicalBase;
    boot_invalidate_start_info(info);
    handoff->Page->Ready = 0;
    status = boot_release_page_tables(services, tables);
    if (status == EFI_SUCCESS)
        status = boot_build_page_tables(services, &plan, handoff->Capabilities->PhysicalBits, 1,
                                        tables);
    if (status == EFI_SUCCESS) status = boot_refresh_final_map(services, handoff->Memory);
    if (status == EFI_SUCCESS) {
        handoff->Descriptors = owner;
        status = boot_check_exit_resources(handoff, &plan);
    }
    if (status == EFI_SUCCESS)
        status = boot_prepare_start_info(&plan, handoff->Tables, handoff->Capabilities, info);
    if (status == EFI_SUCCESS) status = boot_attach_memory_snapshot(info, &handoff->Memory->Snapshot);
    if (status == EFI_SUCCESS) {
        handoff->Page->Ready = 1;
        status = boot_validate_exit_handoff(handoff);
    }
    if (status != EFI_SUCCESS) {
        handoff->Page->Ready = 0;
        boot_invalidate_start_info(info);
        boot_forget_map_snapshot(&handoff->Memory->Snapshot);
        handoff->Descriptors = 0;
    }
    return status;
}
