#include "file_fixture.h"

// ====== verifica errori lettura e rilascio ====== //

__declspec(dllexport) int run_kernel_tests(void)
{
    KERNEL_FILE_REPORT report;
    BOOT_KERNEL_IMAGE retained = {0};
    int test;
    EFI_STATUS expected, status;
    UINTN expected_allocations, expected_frees, offset;
    const int cleanup_cases[] = {35, 36, 41, 43, 44, 45, 46};
    offset = (UINTN)((UINT8 *)&file.GetInfo - (UINT8 *)&file);
    if (offset != 64)
        return 401;
    offset = (UINTN)((UINT8 *)&services.AllocatePool - (UINT8 *)&services);
    if (offset != 64)
        return 402;
    for (test = 0; test <= 24; ++test) {
        prepare(test);
        expected = EFI_LOAD_ERROR;
        if (test == 0) expected = EFI_SUCCESS;
        if (test == 1) expected = EFI_NOT_FOUND;
        if (test == 2 || test == 6 || test == 9 || test == 10 || test == 11 || test == 18)
            expected = EFI_DEVICE_ERROR;
        if (test == 5 || test == 14) expected = EFI_OUT_OF_RESOURCES;
        if (test == 19 || test == 24) expected = EFI_UNSUPPORTED;
        expected_allocations = (test == 0 || (test >= 5 && test <= 11) ||
            (test >= 14 && test <= 16) || test == 20) ? 1 : 0;
        expected_frees = expected_allocations && test != 5 && test != 14 ? 1 : 0;
        if (test == 9) expected_frees = 2;
        status = boot_validate_kernel(&image_token, &table, &report, &validation_owner);
        if (status != expected || mock_error || allocations != expected_allocations ||
            frees != expected_frees || root_closes != (test == 18 ? 0ULL : 1ULL) ||
            file_closes != (test == 1 || test == 18 ? 0ULL : 1ULL) || max_request > 64 * 1024)
            return 410 + test;
        if (status != EFI_SUCCESS && (report.Plan.Entry || report.Plan.SegmentCount))
            return 440;
        if (test == 0 && (report.Validation != ELF_VALID || report.Plan.SegmentCount != 2 ||
            report.FileBytes != 512 || report.Plan.Entry != 0x100100))
            return 441;
    }
    prepare(0);
    chunk = 7;
    if (boot_validate_kernel(&image_token, &table, &report, &validation_owner) != EFI_SUCCESS || position != 512 || frees != 1)
        return 442;
    prepare(0);
    actual_size = reported_size = sizeof(data);
    chunk = sizeof(data);
    if (boot_validate_kernel(&image_token, &table, &report, &validation_owner) != EFI_SUCCESS ||
        max_request != 64 * 1024 || position != sizeof(data) || frees != 1)
        return 448;
    prepare(8);
    kernel_mock_scenario = 9;
    if (boot_validate_kernel(&image_token, &table, &report, &validation_owner) != EFI_LOAD_ERROR || frees != 2 || root_closes != 1)
        return 443;
    prepare(0);
    services.FreePool = 0;
    if (boot_validate_kernel(&image_token, &table, &report, &validation_owner) != EFI_UNSUPPORTED || allocations)
        return 444;
    prepare(0);
    if (boot_validate_kernel(0, &table, &report, &validation_owner) != EFI_INVALID_PARAMETER ||
        boot_validate_kernel(&image_token, &table, 0, &validation_owner) != EFI_INVALID_PARAMETER ||
        boot_validate_kernel(&image_token, 0, &report, &validation_owner) != EFI_INVALID_PARAMETER)
        return 445;
    prepare(0);
    if (efi_main(&image_token, &table) != EFI_SUCCESS || success_messages != 1 ||
        failure_messages || sections != 16 || frees != 1 || root_closes != 2 ||
        kernel_mock_page_allocations <= 3 || kernel_mock_page_frees != kernel_mock_page_allocations)
        return 446;
    prepare(8);
    if (efi_main(&image_token, &table) != EFI_LOAD_ERROR || success_messages ||
        failure_messages != 1 || sections != 3 || frees != 1 || root_closes != 2)
        return 447;
    prepare(25);
    if (efi_main(&image_token, &table) != EFI_OUT_OF_RESOURCES || preparation_failures != 1 ||
        failure_messages != 2 || kernel_mock_page_allocations != 1 || kernel_mock_page_frees != 1 ||
        frees != 1 || file_closes != 1 || root_closes != 2)
        return 449;
    prepare(9);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR || preparation_failures != 1 ||
        failure_messages != 2 || kernel_mock_page_allocations != 2 || kernel_mock_page_frees != 2)
        return 450;
    prepare(0);
    if (boot_load_kernel(&image_token, &table, &report, &retained) != EFI_SUCCESS ||
        kernel_mock_page_allocations != 2 || kernel_mock_page_frees || retained.Pages.Count != 2 ||
        retained.Description.Signature != BOOT_SEGMENTS_SIGNATURE || frees != 1)
        return 451;
    if (boot_load_kernel(&image_token, &table, &report, &retained) != EFI_INVALID_PARAMETER ||
        kernel_mock_page_allocations != 2 || retained.Description.Signature != BOOT_SEGMENTS_SIGNATURE)
        return 452;
    kernel_mock_scenario = 27;
    if (boot_release_kernel(&services, &retained) != EFI_DEVICE_ERROR || retained.Description.Signature ||
        retained.Pages.AllocatedBytes != 4096 || retained.Pages.Segments[0].Pages)
        return 453;
    kernel_mock_scenario = 0;
    if (boot_release_kernel(&services, &retained) != EFI_SUCCESS || retained.Pages.Count ||
        retained.Pages.AllocatedBytes || kernel_mock_page_frees != 3 ||
        boot_release_kernel(&services, &retained) != EFI_SUCCESS || kernel_mock_page_frees != 3)
        return 454;
    prepare(26);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR || kernel_mock_page_frees != 3)
        return 455;
    prepare(28);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR || kernel_mock_page_frees != 2)
        return 456;
    prepare(29);
    if (efi_main(&image_token, &table) != EFI_OUT_OF_RESOURCES || kernel_mock_page_frees != 2)
        return 457;
    prepare(30);
    if (efi_main(&image_token, &table) != EFI_LOAD_ERROR || kernel_mock_page_frees != 3)
        return 458;
    prepare(31);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR ||
        kernel_mock_page_frees != kernel_mock_page_allocations)
        return 459;
    prepare(32);
    if (efi_main(&image_token, &table) != EFI_OUT_OF_RESOURCES || kernel_mock_page_allocations != 5 ||
        kernel_mock_page_frees != 5)
        return 460;
    prepare(33);
    if (efi_main(&image_token, &table) != EFI_UNSUPPORTED || kernel_mock_page_allocations != 3 ||
        kernel_mock_page_frees != 3)
        return 461;
    prepare(34);
    if (efi_main(&image_token, &table) != EFI_LOAD_ERROR || kernel_mock_page_allocations <= 3 ||
        kernel_mock_page_frees != kernel_mock_page_allocations)
        return 462;
    prepare(37);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR || map_retry_releases != 2 ||
        kernel_mock_page_frees != kernel_mock_page_allocations)
        return 465;
    prepare(38);
    if (efi_main(&image_token, &table) != EFI_UNSUPPORTED || kernel_mock_page_allocations != 3 ||
        kernel_mock_page_frees != 3)
        return 466;
    prepare(39);
    if (efi_main(&image_token, &table) != EFI_DEVICE_ERROR ||
        kernel_mock_page_frees != kernel_mock_page_allocations || failure_messages != 1)
        return 467;
    prepare(40);
    if (efi_main(&image_token, &table) != EFI_OUT_OF_RESOURCES || kernel_mock_page_allocations != 3 ||
        kernel_mock_page_frees != 3) return 468;
    for (test = 0; test < sizeof(cleanup_cases) / sizeof(cleanup_cases[0]); ++test) {
        prepare(cleanup_cases[test]);
        expected = cleanup_cases[test] == 46 ? EFI_OUT_OF_RESOURCES : EFI_DEVICE_ERROR;
        if (efi_main(&image_token, &table) != expected ||
            failure_messages != ((cleanup_cases[test] == 35) ? 2U : 1U) ||
            kernel_mock_page_allocations <= 3 || kernel_mock_page_frees != kernel_mock_page_allocations +
            ((cleanup_cases[test] == 35) ? 1U : 0U)) return 471 + cleanup_cases[test];
    }
    return validation_cleanup_error ? 490 : 0;
}

// ====== riuso del fixture completo per guasti delle nuove risorse ====== //

EFI_STATUS test_run_descriptor_scenario(int scenario)
{
    prepare(0);
    kernel_mock_scenario = scenario;
    kernel_mock_bad_address = ((UINT64)(UINTN)&services) & ~4095ULL;
    return efi_main(&image_token, &table);
}

UINT32 test_descriptor_phase(void)
{
    return (UINT32)sections;
}
