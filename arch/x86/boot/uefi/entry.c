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
#include "handoff/descriptors/prepare.h"
#include "handoff/descriptors/exceptions.h"
#include "handoff/transition/activation.h"
#include "acpi/discovery.h"
#include "graphics/handoff.h"

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
    BOOT_EXIT_HANDOFF handoff = {kernel, stack, caps, tables, page, memory, 0};
    return boot_validate_exit_handoff(&handoff);
}

// ====== risorse persistenti prima del futuro salto al kernel ====== //

static EFI_STATUS prepare_descriptors(EFI_SYSTEM_TABLE *system_table, const BOOT_KERNEL_IMAGE *kernel,
    const BOOT_KERNEL_STACK *stack, const BOOT_PAGING_CAPABILITIES *caps, BOOT_PAGE_TABLES *tables,
    BOOT_START_PAGE *page, BOOT_FINAL_MEMORY_MAP *memory, BOOT_DESCRIPTOR_STORAGE *owner)
{
    BOOT_EXIT_HANDOFF handoff = {kernel, stack, caps, tables, page, memory, 0};
    EFI_STATUS status = boot_allocate_descriptor_storage(system_table, &handoff, owner);
    if (status == EFI_SUCCESS) status = boot_prepare_descriptor_handoff(system_table, &handoff, owner, tables);
    if (status == EFI_SUCCESS) status = boot_prepare_activation(&handoff);
    return status;
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
    BOOT_GDT_DRAFT descriptor_draft;
    BOOT_IDT_DRAFT exception_draft;
    BOOT_DESCRIPTOR_STORAGE descriptor_storage = {0};
    static BOOT_GOP_OWNER graphics;
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
                      L"Mappa persistente R/NX e contratto v4 verificati; tutte le risorse coperte.");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Capacita buffer mappa persistente: ", final_map.Snapshot.Capacity);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log(system_table->ConOut, BOOT_OK, L"Preflight uscita UEFI verificato; servizi firmware ancora attivi.");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_prepare_gdt_draft(system_table->ConOut, &descriptor_draft);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_prepare_idt_draft(system_table->ConOut, &exception_draft);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_section(system_table->ConOut, L"descrittori e stack persistenti");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = prepare_descriptors(system_table, &loaded, &stack, &capabilities, &tables,
                                 &start_page, &final_map, &descriptor_storage);
    if (status == EFI_SUCCESS) status = boot_read_paging_state(&capabilities, &paging_after);
    if (status == EFI_SUCCESS) status = boot_compare_paging_state(&paging_before, &paging_after);
    if (status != EFI_SUCCESS) {
        boot_log(system_table->ConOut, BOOT_FAIL, L"preparazione risorse descrittori fallita.");
        goto cleanup_kernel;
    }
    status = boot_log(system_table->ConOut, BOOT_OK,
        L"Descrittori e due stack RW/NX, codice persistente R/X verificati; guard page assenti, CR3 invariato.");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Root finale delle tabelle inattive: ", tables.Root);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log_hex(system_table->ConOut, L"Pagine descrittori e stack persistenti: ", descriptor_storage.Pages);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_prepare_acpi_handoff(system_table, &final_map.Snapshot, capabilities.PhysicalBits, &start_page);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_gop_choose(system_table,capabilities.PhysicalBits,&graphics);
    if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) status = EFI_SUCCESS;
    if (status == EFI_SUCCESS)
        status = boot_attach_framebuffer_snapshot((BOOT_START_INFO *)(UINTN)start_page.PhysicalBase,
            &final_map.Snapshot,&graphics.Framebuffer,capabilities.PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_read_paging_state(&capabilities,&paging_after);
    if (status == EFI_SUCCESS) status = boot_compare_paging_state(&paging_before,&paging_after);
    if (status != EFI_SUCCESS) goto cleanup_kernel;
    status = boot_log(system_table->ConOut,graphics.Valid ? BOOT_OK : BOOT_WARN,
        graphics.Valid ? L"FRAMEBUFFER_HANDOFF_PRESENT" : L"FRAMEBUFFER_HANDOFF_ABSENT");
    if (status != EFI_SUCCESS) goto cleanup_kernel;
#ifdef BOOT_RUN_KERNEL
    {
        BOOT_EXIT_HANDOFF handoff = {&loaded, &stack, &capabilities, &tables, &start_page, &final_map, &descriptor_storage};
        BOOT_EXIT_STATE exit_state = {0};
        status = boot_prepare_activation(&handoff);
        if (status != EFI_SUCCESS) goto cleanup_kernel;
        status = boot_log_hex(system_table->ConOut, L"ACTIVATION_CODE: ", descriptor_storage.CodeBase);
        if (status == EFI_SUCCESS) status = boot_log_hex(system_table->ConOut, L"ACTIVATION_ROOT: ", tables.Root);
        if (status == EFI_SUCCESS) status = boot_log_hex(system_table->ConOut, L"ACTIVATION_DATA: ", descriptor_storage.PhysicalBase);
        if (status == EFI_SUCCESS) status = boot_log_hex(system_table->ConOut, L"ACTIVATION_EXIT_STATE: ", (UINT64)(UINTN)&exit_state);
        if (status == EFI_SUCCESS) status = boot_log(system_table->ConOut, BOOT_INFO, L"KERNEL_HANDOFF_READY");
        if (status != EFI_SUCCESS) goto cleanup_kernel;
        status = boot_exit_memory_handoff(image, (EFI_BOOT_SERVICES_EXIT_PREFIX *)system_table->BootServices,
                                          &handoff, &exit_state);
        if (exit_state.Phase == BOOT_SERVICES_EXITED)
            x86_enter_transition(descriptor_storage.PhysicalBase + BOOT_ACTIVATION_CONTEXT_OFFSET,
                                  descriptor_storage.CodeBase + BOOT_ACTIVATION_OFFSET);
        if (exit_state.Phase == BOOT_SERVICES_RESTRICTED) x86_stop_transition();
        goto cleanup_kernel;
    }
#else
    status = boot_log(system_table->ConOut, BOOT_WARN, L"Percorso diagnostico: cleanup senza attivazione del kernel.");
#endif

cleanup_kernel:
    if (graphics.Pending) {
        cleanup_status = boot_gop_release_pending(&graphics);
        if (cleanup_status != EFI_SUCCESS) {
            (void)boot_log(system_table->ConOut,BOOT_FAIL,L"GOP_BUFFER_RELEASE_FAILED");
            x86_stop_transition();
        }
    }
    cleanup_status = boot_gop_abandon(&graphics);
    if (cleanup_status != EFI_SUCCESS) {
        (void)boot_log(system_table->ConOut,BOOT_FAIL,L"GOP_MODE_RESTORE_FAILED");
        x86_stop_transition();
    }
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
    cleanup_status = boot_release_descriptor_storage((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &descriptor_storage);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio risorse descrittori fallito.");
    if (status == EFI_SUCCESS) status = cleanup_status;
    cleanup_status = boot_release_stack((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &stack);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio stack kernel fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    cleanup_status = boot_release_kernel((EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices, &loaded);
    if (cleanup_status != EFI_SUCCESS)
        boot_log(system_table->ConOut, BOOT_FAIL, L"rilascio risorse kernel fallito.");
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    if (had_kernel_pages && !loaded.Pages.Count && !loaded.FileBuffer && !stack.Pages && !tables.Count && !start_page.Pages && !final_map.Pages && !descriptor_storage.Pages) {
        cleanup_status = boot_log(system_table->ConOut, BOOT_OK, L"Pagine kernel rilasciate prima del ritorno al firmware.");
        if (status == EFI_SUCCESS)
            status = cleanup_status;
    }
    return status;
}
