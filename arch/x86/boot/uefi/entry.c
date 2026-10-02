#include "file_protocol.h"
#include "console/log.h"
#include "storage/kernel_file.h"
#include "memory/map.h"
#include "memory/regions.h"
#include "handoff/resources.h"
#include "paging/plan.h"
#include "cpu/paging_capabilities.h"
#include "paging/preparation.h"
#include "cpu/paging_state.h"
#include "handoff/protocol/prepare.h"
#include "handoff/firmware/prepare_map.h"
#include "memory/final/mapping.h"
#include "handoff/firmware/terminal/handoff.h"

// ====== verifica del piano senza aumentare lo stack di ingresso ====== //

static EFI_STATUS check_mapping_plan(const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack)
{
    BOOT_MAPPING_PLAN plan;
    return boot_plan_mappings(kernel, stack, &plan);
}

// ====== preflight senza tentare ExitBootServices nel percorso di prova ====== //

static EFI_STATUS check_exit_handoff(const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                    const BOOT_PAGING_CAPABILITIES *caps, const BOOT_PAGE_TABLES *tables,
                                    BOOT_START_PAGE *page, BOOT_FINAL_MEMORY_MAP *memory)
{
    BOOT_EXIT_HANDOFF handoff = {kernel, stack, caps, tables, page, memory};
    return boot_validate_exit_handoff(&handoff);
}

// ====== ingresso applicazione UEFI ====== //

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    EFI_STATUS status;
    KERNEL_FILE_REPORT kernel;
    BOOT_KERNEL_IMAGE loaded = {0};
    BOOT_MEMORY_MAP map = {0};
    BOOT_MEMORY_SUMMARY summary;
    BOOT_KERNEL_STACK stack = {0};
    EFI_STATUS cleanup_status;
    BOOT_PAGING_CAPABILITIES capabilities;
    BOOT_PAGE_TABLES tables = {0};
    BOOT_START_PAGE start_page = {0};
    BOOT_FINAL_MEMORY_MAP final_map = {0};
    BOOT_PAGING_STATE paging_before, paging_after;
    int had_kernel_pages;

    if (!system_table)
        return EFI_INVALID_PARAMETER;
    if (system_table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        system_table->Hdr.HeaderSize < sizeof(EFI_SYSTEM_TABLE))
        return EFI_INVALID_PARAMETER;
    if (!system_table->ConOut || !system_table->ConOut->OutputString)
        return EFI_UNSUPPORTED;

    status = boot_section(system_table->ConOut, L"avvio UEFI");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_log(system_table->ConOut, BOOT_OK, L"avvio UEFI riuscito.");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_section(system_table->ConOut, L"verifica file di avvio");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_log(system_table->ConOut, BOOT_INFO, L"Lettura di EFI/BOOT/boot-check.txt");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_check_file(image, system_table);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"verifica file fallita.");
        return status;
    }
    status = boot_log(system_table->ConOut, BOOT_OK, L"lettura file verificata.");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_section(system_table->ConOut, L"validazione kernel");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_log(system_table->ConOut, BOOT_INFO, L"Lettura e controllo di EFI/BOOT/kernel.elf");
    if (status != EFI_SUCCESS)
        return status;
    status = boot_load_kernel(image, system_table, &kernel, &loaded);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, kernel.Validation == ELF_VALID ?
            L"preparazione kernel fallita." : L"validazione kernel fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK, L"ELF64 verificato; buffer e file rilasciati.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"stack iniziale kernel");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_allocate_stack((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &loaded, &stack);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"allocazione stack kernel fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK, L"Stack dedicato di 64 KiB allocato e azzerato.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"mappa memoria UEFI");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_acquire_memory_map((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &map);
    if (status == EFI_SUCCESS)
        status = boot_summarize_memory(&map, &summary);
    if (status == EFI_SUCCESS)
        status = boot_check_resources(&map, &loaded, &stack);
    if (status == EFI_SUCCESS)
        status = check_mapping_plan(&loaded, &stack);
    cleanup_status = boot_release_memory_map((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &map);
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"verifica mappa memoria fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK, L"Mappa memoria verificata; buffer temporaneo rilasciato.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Regioni classificate; allocazioni ancora gestite da UEFI.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Kernel e stack coperti dalla mappa come EfiLoaderData.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"piano mappature kernel");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Piano verificato; pagina di guardia riservata, tabelle ancora inattive.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"capacita paging CPU");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_read_paging_capabilities(&capabilities);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"CPU non compatibile con il profilo paging richiesto.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK, L"Long mode, NX e larghezze degli indirizzi verificati via CPUID.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"stato paging e CR3 firmware");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_read_paging_state(&capabilities, &paging_before);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"stato paging firmware non supportato.");
        goto cleanup_kernel;
    }
    status = boot_log_hex(system_table->ConOut, L"CR3 firmware: ", paging_before.Cr3);
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Flag NXE=1 WP=2 PCID=4: ", paging_before.Flags);
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Paging attivo a quattro livelli verificato; CR3 letto senza modifiche.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"costruzione tabelle di pagina");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_prepare_handoff_tables((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices,
                                     &loaded, &stack, &capabilities, &tables, &map, &start_page);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"costruzione o copertura delle tabelle fallita.");
        goto cleanup_kernel;
    }
    status = boot_read_paging_state(&capabilities, &paging_after);
    if (status == EFI_SUCCESS)
        status = boot_compare_paging_state(&paging_before, &paging_after);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"CR3 o stato paging cambiato durante la preparazione.");
        goto cleanup_kernel;
    }
    status = boot_log_hex(system_table->ConOut, L"CR3 firmware dopo: ", paging_after.Cr3);
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Root delle tabelle inattive: ", tables.Root);
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Tabelle costruite e coperte da EfiLoaderData; CR3 invariato.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"contratto ingresso kernel");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = start_page.Ready == 1 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"preparazione contratto ingresso fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK,
                      L"Contratto su pagina R/NX e stack ABI verificati; tabelle inattive.");
    if (status != EFI_SUCCESS)
        goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"mappa persistente per uscita UEFI");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_prepare_final_memory_map((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices,
                                          &loaded, &stack, &capabilities, &tables, &start_page, &final_map);
    if (status == EFI_SUCCESS)
        status = boot_prepare_memory_handoff((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices,
                                             &loaded, &stack, &capabilities, &tables, &start_page, &final_map);
    if (status == EFI_SUCCESS) status = boot_read_paging_state(&capabilities, &paging_after);
    if (status == EFI_SUCCESS) status = boot_compare_paging_state(&paging_before, &paging_after);
    if (status == EFI_SUCCESS)
        status = check_exit_handoff(&loaded, &stack, &capabilities, &tables, &start_page, &final_map);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"preparazione mappa persistente fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK,
                      L"Mappa persistente R/NX e contratto v2 verificati; tutte le risorse coperte.");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Capacita buffer mappa persistente: ", final_map.Snapshot.Capacity);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Preflight uscita UEFI verificato; servizi firmware ancora attivi.");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_WARN, L"Avvio del kernel non ancora implementato.");

cleanup_kernel:
    had_kernel_pages = loaded.Pages.Count != 0;
    if (start_page.Ready == 1) {
        boot_invalidate_start_info((BOOT_START_INFO *)(UINTN)start_page.PhysicalBase);
        start_page.Ready = 0;
    }
    cleanup_status = boot_release_final_map((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &final_map);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio pagine mappa persistente fallito.");
    if (status == EFI_SUCCESS) status = cleanup_status;
    cleanup_status = boot_release_memory_map((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &map);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio buffer mappa memoria fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    cleanup_status = boot_release_page_tables((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &tables);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio tabelle di pagina fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    cleanup_status = boot_release_start_page((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &start_page);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio pagina contratto fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    cleanup_status = boot_release_stack((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &stack);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio stack kernel fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    cleanup_status = boot_release_kernel((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &loaded);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio pagine kernel fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    if (had_kernel_pages && !loaded.Pages.Count && !stack.Pages && !tables.Count && !start_page.Pages && !final_map.Pages) {
        cleanup_status = boot_log(system_table->ConOut, BOOT_OK, L"Pagine kernel rilasciate prima del ritorno al firmware.");
        if (status == EFI_SUCCESS)
            status = cleanup_status;
    }
    return status;
}

