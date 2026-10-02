param([string]$ToolchainBin)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'status.ps1')
try {

    # ====== ricerca compilatore ====== #

    if (!$ToolchainBin) {
        $base = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
        $version = Get-ChildItem -LiteralPath $base -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if (!$version) { throw 'Specificare -ToolchainBin con cl.exe e link.exe x64.' }
        $ToolchainBin = Join-Path $version.FullName 'bin\Hostx64\x64'
    }
    Write-BootSection 'ricerca compilatore'
    Write-BootStatus INFO "Toolchain: $ToolchainBin"
    $compiler = Join-Path $ToolchainBin 'cl.exe'
    $linker = Join-Path $ToolchainBin 'link.exe'
    $librarian = Join-Path $ToolchainBin 'lib.exe'
    $compilerInclude = (Resolve-Path (Join-Path $ToolchainBin '..\..\..\include')).Path
    $bootRoot = Split-Path -Parent $PSScriptRoot
    $projectRoot = (Resolve-Path (Join-Path $bootRoot '..\..\..\..')).Path
    $processorLibrary = Join-Path $projectRoot 'arch\x86\lib\processor'
    . (Join-Path $bootRoot 'test\fixtures\elf_fixture.ps1')
    $output = Join-Path $bootRoot 'build'
    $esp = Join-Path $output 'esp\EFI\BOOT'
    New-Item -ItemType Directory -Force -Path $esp | Out-Null

    # ====== compilazione senza runtime ====== #

    function Invoke-Checked([string]$Program, [string[]]$Arguments) {
        $messages = & $Program @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        foreach ($message in $messages) {
            Write-BootStatus $(if ($exitCode -eq 0) { 'INFO' } else { 'FAIL' }) "$message"
        }
        if ($exitCode -ne 0) { throw "Errore in $Program ($exitCode)" }
    }
    Write-BootSection 'compilazione bootloader e test'
    $rustc = (Get-Command rustc -ErrorAction Stop).Source
    $memoryLibrary = Join-Path $projectRoot 'lib\memory'
    Invoke-Checked $rustc @('--edition=2024', '--crate-type=lib', '--emit=obj', '--target', 'x86_64-pc-windows-msvc', '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings', (Join-Path $memoryLibrary 'rust\capacity\buffer_capacity.rs'), '-o', "$output\buffer_capacity.obj")
    Invoke-Checked $rustc @('--edition=2024', '--test', '--target', 'x86_64-pc-windows-msvc', '-D', 'warnings', (Join-Path $memoryLibrary 'test\rust\capacity\buffer_capacity_test.rs'), '-o', "$output\buffer_capacity_tests.exe")
    Invoke-Checked "$output\buffer_capacity_tests.exe" @()
    $segmentSource = Join-Path $bootRoot 'elf\rust\validation\segment_bounds.rs'
    Invoke-Checked $rustc @('--edition=2024', '--crate-type=lib', '--emit=obj', '--target', 'x86_64-pc-windows-msvc', '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings', $segmentSource, '-o', "$output\segment_bounds.obj")
    Invoke-Checked $rustc @('--edition=2024', '--test', '--target', 'x86_64-pc-windows-msvc', '-D', 'warnings', (Join-Path $bootRoot 'test\elf\rust\validation\segment_bounds_test.rs'), '-o', "$output\segment_bounds_tests.exe")
    Invoke-Checked "$output\segment_bounds_tests.exe" @()
    Invoke-Checked $rustc @('--edition=2024', '--crate-type=lib', '--emit=obj', '--target', 'x86_64-pc-windows-msvc', '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings', (Join-Path $processorLibrary 'rust\paging\paging_capabilities.rs'), '-o', "$output\cpu_profile.obj")
    Invoke-Checked $rustc @('--edition=2024', '--test', '--target', 'x86_64-pc-windows-msvc', '-D', 'warnings', (Join-Path $bootRoot 'test\cpu\rust\paging\paging_capabilities_test.rs'), '-o', "$output\cpu_profile_tests.exe")
    Invoke-Checked "$output\cpu_profile_tests.exe" @()
    Invoke-Checked $rustc @('--edition=2024', '--crate-type=lib', '--emit=obj', '--target', 'x86_64-pc-windows-msvc', '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings', (Join-Path $processorLibrary 'rust\paging\paging_state.rs'), '-o', "$output\paging_state.obj")
    Invoke-Checked $rustc @('--edition=2024', '--test', '--target', 'x86_64-pc-windows-msvc', '-D', 'warnings', (Join-Path $bootRoot 'test\cpu\rust\paging\paging_state_test.rs'), '-o', "$output\paging_state_tests.exe")
    Invoke-Checked "$output\paging_state_tests.exe" @()
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/GS-', '/Zl', '/Od', '/c', ('/I' + $bootRoot), ('/I' + $projectRoot), ('/I' + $compilerInclude))
    Invoke-Checked $compiler ($flags + @((Join-Path $projectRoot 'lib\memory\address_ranges.c'), "/Fo$output\address_ranges.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $projectRoot 'lib\memory\test\address_ranges_test.c'), "/Fo$output\address_ranges_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $processorLibrary 'processor_access.c'), "/Fo$output\processor_access.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $projectRoot 'arch\x86\lib\descriptors\gdt.c'), "/Fo$output\gdt.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\descriptors\prepare.c'), "/Fo$output\gdt_prepare.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\gdt_test.c'), "/Fo$output\gdt_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $projectRoot 'arch\x86\lib\descriptors\idt.c'), "/Fo$output\idt.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\idt_test.c'), "/Fo$output\idt_test.obj"))
    Invoke-Checked (Join-Path $ToolchainBin 'ml64.exe') @('/nologo', '/W3', '/WX', '/c', "/Fo$output\exception_entries.obj", (Join-Path $bootRoot 'handoff\descriptors\assembly\exceptions\terminal.asm'))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\descriptors\exceptions.c'), "/Fo$output\idt_prepare.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\exceptions_test.c'), "/Fo$output\exception_entries_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $projectRoot 'arch\x86\lib\descriptors\tss.c'), "/Fo$output\tss.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\tss_test.c'), "/Fo$output\tss_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\descriptors\transition\bundle.c'), "/Fo$output\descriptor_bundle.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\descriptors\transition\storage.c'), "/Fo$output\descriptor_storage.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\transition\storage_test.c'), "/Fo$output\descriptor_storage_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\descriptors\transition\preparation.c'), "/Fo$output\descriptor_preparation.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\descriptors\transition\bundle_test.c'), "/Fo$output\descriptor_bundle_test.obj"))
    Invoke-Checked $librarian @('/nologo', "/out:$output\x86_descriptors.lib", "$output\gdt.obj", "$output\idt.obj", "$output\tss.obj")
    Invoke-Checked $librarian @('/nologo', "/out:$output\os_memory.lib", "$output\address_ranges.obj", "$output\buffer_capacity.obj")
    Invoke-Checked $librarian @('/nologo', "/out:$output\x86_processor.lib", "$output\processor_access.obj", "$output\cpu_profile.obj", "$output\paging_state.obj")
    $cppFlags = @('/nologo', '/TP', '/std:c++17', '/W4', '/WX', '/GS-', '/Zl', '/Od', '/c', ('/I' + $projectRoot), ('/I' + $compilerInclude))
    Invoke-Checked $compiler ($cppFlags + @((Join-Path $bootRoot 'test\libraries\cpp\interfaces\shared_interface_test.cpp'), "/Fo$output\shared_interface_test.obj"))
    & (Join-Path $bootRoot 'test\configs\profile_test.ps1') -ToolchainBin $ToolchainBin
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\protocol\prepare.c'), "/Fo$output\start_info.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\protocol\page.c'), "/Fo$output\start_page.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\start_page_test.c'), "/Fo$output\start_page_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\start_info_test.c'), "/Fo$output\start_info_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'cpu\paging_capabilities.c'), "/Fo$output\cpu_capabilities.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\cpu\paging_capabilities_test.c'), "/Fo$output\cpu_tests.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\cpu\paging_state_test.c'), "/Fo$output\paging_state_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'cpu\processor_query.c'), "/Fo$output\processor_query.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\cpu\processor_mock.c'), "/Fo$output\processor_mock.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\paging\preparation_test.c'), "/Fo$output\preparation_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'paging\preparation.c'), "/Fo$output\paging_preparation.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'entry.c'), "/Fo$output\entry.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'file_probe.c'), "/Fo$output\file_probe.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'console\log.c'), "/Fo$output\log.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'storage\kernel_file.c'), "/Fo$output\kernel_file.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'elf\validate.c'), "/Fo$output\elf_validate.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'elf\load.c'), "/Fo$output\elf_load.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\segments.c'), "/Fo$output\handoff_segments.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\map.c'), "/Fo$output\memory_map.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\final\refresh.c'), "/Fo$output\map_refresh.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\final\storage.c'), "/Fo$output\final_storage.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\firmware\prepare_map.c'), "/Fo$output\final_map.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\final\mapping.c'), "/Fo$output\memory_handoff.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\firmware\terminal\handoff.c'), "/Fo$output\terminal_handoff.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\memory\final_storage_test.c'), "/Fo$output\final_storage_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\firmware\final_map_test.c'), "/Fo$output\final_map_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\firmware\memory_handoff_test.c'), "/Fo$output\memory_handoff_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\firmware\exit_services.c'), "/Fo$output\exit_services.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\memory\refresh_test.c'), "/Fo$output\map_refresh_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\firmware\exit_services_test.c'), "/Fo$output\exit_services_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\regions.c'), "/Fo$output\memory_regions.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'storage\kernel_image.c'), "/Fo$output\kernel_image.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'storage\kernel_buffer.c'), "/Fo$output\kernel_buffer.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\stack.c'), "/Fo$output\handoff_stack.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'handoff\resources.c'), "/Fo$output\handoff_resources.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'memory\ownership.c'), "/Fo$output\memory_ownership.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'paging\tables.c'), "/Fo$output\paging_tables.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'paging\verification\contents.c'), "/Fo$output\table_contents.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\paging\tables_test.c'), "/Fo$output\tables_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'paging\plan.c'), "/Fo$output\paging_plan.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\paging\plan_test.c'), "/Fo$output\plan_test.obj"))
    Invoke-Checked $linker @('/nologo', '/machine:x64', '/subsystem:efi_application', '/entry:efi_main', "$output\processor_query.obj", '/nodefaultlib', '/incremental:no', '/dynamicbase:no', '/fixed:no', "/out:$esp\BOOTX64.EFI", "$output\entry.obj", "$output\file_probe.obj", "$output\log.obj", "$output\kernel_file.obj", "$output\elf_validate.obj", "$output\segment_bounds.obj", "$output\cpu_capabilities.obj", "$output\x86_processor.lib", "$output\os_memory.lib", "$output\paging_preparation.obj", "$output\start_info.obj", "$output\start_page.obj", "$output\elf_load.obj", "$output\handoff_segments.obj", "$output\memory_map.obj", "$output\map_refresh.obj", "$output\final_storage.obj", "$output\final_map.obj", "$output\memory_handoff.obj", "$output\terminal_handoff.obj", "$output\exit_services.obj", "$output\memory_regions.obj", "$output\kernel_image.obj", "$output\kernel_buffer.obj", "$output\handoff_stack.obj", "$output\handoff_resources.obj", "$output\memory_ownership.obj", "$output\paging_plan.obj", "$output\paging_tables.obj", "$output\table_contents.obj", "$output\x86_descriptors.lib", "$output\descriptor_bundle.obj", "$output\descriptor_storage.obj", "$output\descriptor_preparation.obj", "$output\gdt_prepare.obj", "$output\idt_prepare.obj", "$output\exception_entries.obj")
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\entry_test.c'), "/Fo$output\tests.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\file_test.c'), "/Fo$output\file_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\elf_test.c'), "/Fo$output\elf_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\log_test.c'), "/Fo$output\log_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\storage\kernel_file_test.c'), "/Fo$output\kernel_file_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\storage\kernel_buffer_test.c'), "/Fo$output\kernel_buffer_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\elf\load_test.c'), "/Fo$output\load_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\segments_test.c'), "/Fo$output\handoff_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\memory\map_test.c'), "/Fo$output\map_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\memory\map_validation_test.c'), "/Fo$output\map_validation_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\memory\regions_test.c'), "/Fo$output\regions_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\storage\kernel_memory_mock.c'), "/Fo$output\kernel_memory_mock.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\stack_test.c'), "/Fo$output\stack_test.obj"))
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\handoff\resources_test.c'), "/Fo$output\resources_test.obj"))
    & (Join-Path $PSScriptRoot 'build-kernel-probe.ps1') -ToolchainBin $ToolchainBin
    Invoke-Checked $compiler ($flags + @((Join-Path $bootRoot 'test\kernel\entry_test.c'), "/Fo$output\kernel_probe_test.obj"))
    $testId = [Guid]::NewGuid().ToString('N')
    $testDll = Join-Path $output "uefi_tests_$testId.dll"
    Invoke-Checked $linker @('/nologo', '/machine:x64', '/dll', '/noentry', '/nodefaultlib', '/incremental:no', "/out:$testDll", "$output\gdt_test.obj", "$output\idt_test.obj", "$output\tss_test.obj", "$output\descriptor_bundle_test.obj", "$output\descriptor_storage_test.obj", "$output\exception_entries_test.obj", "$output\map_validation_test.obj", "$output\final_storage_test.obj", "$output\final_map_test.obj", "$output\memory_handoff_test.obj", "$output\address_ranges_test.obj", "$output\shared_interface_test.obj", "$output\map_refresh_test.obj", "$output\exit_services_test.obj", "$output\start_page_test.obj", "$output\start_info_test.obj", "$output\kernel_probe.obj", "$output\kernel_probe_test.obj", "$output\tests.obj", "$output\file_test.obj", "$output\entry.obj", "$output\file_probe.obj", "$output\elf_validate.obj", "$output\segment_bounds.obj", "$output\cpu_capabilities.obj", "$output\x86_processor.lib", "$output\os_memory.lib", "$output\paging_preparation.obj", "$output\start_info.obj", "$output\start_page.obj", "$output\elf_test.obj", "$output\log.obj", "$output\log_test.obj", "$output\kernel_file.obj", "$output\kernel_file_test.obj", "$output\kernel_buffer_test.obj", "$output\elf_load.obj", "$output\load_test.obj", "$output\handoff_segments.obj", "$output\handoff_test.obj", "$output\memory_map.obj", "$output\map_refresh.obj", "$output\final_storage.obj", "$output\final_map.obj", "$output\memory_handoff.obj", "$output\terminal_handoff.obj", "$output\exit_services.obj", "$output\map_test.obj", "$output\memory_regions.obj", "$output\regions_test.obj", "$output\kernel_memory_mock.obj", "$output\stack_test.obj", "$output\resources_test.obj", "$output\plan_test.obj", "$output\tables_test.obj", "$output\preparation_test.obj", "$output\cpu_tests.obj", "$output\paging_state_test.obj", "$output\processor_mock.obj", "$output\kernel_image.obj", "$output\kernel_buffer.obj", "$output\handoff_stack.obj", "$output\handoff_resources.obj", "$output\memory_ownership.obj", "$output\paging_plan.obj", "$output\paging_tables.obj", "$output\table_contents.obj", "$output\x86_descriptors.lib", "$output\descriptor_bundle.obj", "$output\descriptor_storage.obj", "$output\descriptor_preparation.obj", "$output\gdt_prepare.obj", "$output\idt_prepare.obj", "$output\exception_entries.obj")
    [IO.File]::WriteAllText((Join-Path $esp 'boot-check.txt'), "OS boot check`r`n", [Text.Encoding]::ASCII)
    Copy-Item -LiteralPath "$output\kernel_probe.elf" -Destination (Join-Path $esp 'kernel.elf')
    Write-BootStatus INFO 'kernel.elf contiene il probe C: caricabile, salto ancora disabilitato.'
    Write-BootStatus OK 'Compilazione e collegamento completati.'

    # ====== esecuzione test C ====== #

    $dll = $testDll.Replace('\', '\\')
    $className = 'UefiTests' + $testId
    $definition = 'using System.Runtime.InteropServices; public static class {0} {{ [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_kernel_buffer_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_gdt_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_idt_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_tss_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_descriptor_bundle_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_descriptor_storage_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_exception_entry_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_map_validation_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_final_storage_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_final_map_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_memory_handoff_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_address_ranges_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_shared_interface_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_map_refresh_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_exit_services_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_table_contents_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_start_page_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_start_info_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_kernel_probe_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_kernel_probe_elf(byte[] bytes, ulong size); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_elf_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_log_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_kernel_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_load_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_handoff_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_map_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_region_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_stack_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_resource_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_plan_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_table_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_cpu_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_preparation_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_paging_state_tests(); [DllImport("{1}", CallingConvention=CallingConvention.Cdecl)] public static extern int run_hex_log_tests(); }}' -f $className, $dll
    $native = Add-Type -TypeDefinition $definition -PassThru
    Write-BootSection 'test automatici C'
    $bufferResult = $native::run_kernel_buffer_tests()
    if ($bufferResult -ne 0) { throw "Test buffer kernel fallito: caso $bufferResult" }
    Write-BootStatus OK 'Buffer kernel: alias, overflow, proprieta e rilascio combinato ritentabile verificati.'
    $exceptionResult = $native::run_exception_entry_tests()
    if ($exceptionResult -ne 0) { throw "Test ingressi eccezioni fallito: caso $exceptionResult" }
    Write-BootStatus OK '32 ingressi assembly: codice macchina, vettori, salti e IDT verificati senza esecuzione privilegiata.'
    $storageResult = $native::run_descriptor_storage_tests()
    if ($storageResult -ne 0) { throw "Test risorse descrittori fallito: caso $storageResult" }
    Write-BootStatus OK 'Descrittori persistenti: ownership, retry, guardie, allocazione, rebuild e copertura verificati.'
    $bundleResult = $native::run_descriptor_bundle_tests()
    if ($bundleResult -ne 0) { throw "Test bundle descrittori fallito: caso $bundleResult" }
    Write-BootStatus OK 'Bundle GDT/IDT/TSS: preparazione, verifica incrociata e invalidazione verificate.'
    $tssResult = $native::run_tss_tests()
    if ($tssResult -ne 0) { throw "Test TSS fallito: caso $tssResult" }
    Write-BootStatus OK 'TSS: 960 mutazioni, campi, alias e stack distinti verificati; LTR non eseguito.'
    $idtResult = $native::run_idt_tests()
    if ($idtResult -ne 0) { throw "Test descrittori IDT fallito: caso $idtResult" }
    Write-BootStatus OK 'IDT/IDTR: 4176 mutazioni, indirizzi, sentinelle e alias verificati.'
    $gdtResult = $native::run_gdt_tests()
    if ($gdtResult -ne 0) { throw "Test descrittori GDT fallito: caso $gdtResult" }
    Write-BootStatus OK 'GDT/GDTR: byte, bit, alias, limiti e 512 basi riproducibili verificati.'
    $validationResult = $native::run_map_validation_tests()
    if ($validationResult -ne 0) { throw "Test revisione memoria fallito: caso $validationResult" }
    Write-BootStatus OK 'Revisione memoria: buffer non dereferenziabili, alias, 4096 descrittori e 512 mappe generate verificati.'
    $finalStorageResult = $native::run_final_storage_tests()
    if ($finalStorageResult -ne 0) { throw "Test pagine mappa persistente fallito: caso $finalStorageResult" }
    $handoffResult = $native::run_memory_handoff_tests()
    if ($handoffResult -ne 0) { throw "Test mappa R/NX e contratto v2 fallito: caso $handoffResult" }
    Write-BootStatus OK 'Mappa R/NX e contratto v2: foglie, preflight e 16 scenari di uscita terminale simulata verificati.'
    $finalMapResult = $native::run_final_map_tests()
    if ($finalMapResult -ne 0) { throw "Test copertura mappa persistente fallito: caso $finalMapResult" }
    Write-BootStatus OK 'Mappa persistente: pagine, alias, limiti, copertura di tutte le risorse e rilascio ritentabile verificati.'
    $rangeResult = $native::run_address_ranges_tests()
    if ($rangeResult -ne 0) { throw "Test libreria intervalli fallito: caso $rangeResult" }
    $interfaceResult = $native::run_shared_interface_tests()
    if ($interfaceResult -ne 0) { throw "Test interfacce condivise C++ fallito: caso $interfaceResult" }
    Write-BootStatus OK 'Librerie condivise: confini, 73984 confronti e collegamento C/C++/Rust verificati.'
    if ($native::run_kernel_probe_tests() -ne 0) { throw 'Test ingresso kernel diagnostico fallito.' }
    $probeBytes = [IO.File]::ReadAllBytes("$output\kernel_probe.elf")
    if ($native::run_kernel_probe_elf($probeBytes, [uint64]$probeBytes.Length) -ne 0) { throw 'ELF diagnostico non conforme.' }
    Write-BootStatus OK 'Ingresso C eseguito su host; contratto, ELF reale e BSS verificati.'
    if ($native::run_start_info_tests() -ne 0) { throw 'Test contratto preparatorio e stack ABI fallito.' }
    Write-BootStatus OK 'Contratto preparatorio, invalidazione e stack ABI verificati.'
    if ($native::run_start_page_tests() -ne 0) { throw 'Test pagina contratto fallito.' }
    Write-BootStatus OK 'Pagina contratto: R/NX, alias, rollback e rilascio ritentabile verificati.'
    $refreshResult = $native::run_map_refresh_tests()
    if ($refreshResult -ne 0) { throw "Test refresh mappa fallito: caso $refreshResult" }
    $exitResult = $native::run_exit_services_tests()
    if ($exitResult -ne 0) { throw "Test ExitBootServices simulato fallito: caso $exitResult" }
    Write-BootStatus OK 'Refresh senza allocazioni e uscita firmware simulata: sequenza, chiavi e stati verificati.'
    $result = $native::run_tests()
    if ($result -ne 0) { throw "Test C fallito: caso $result" }
    Write-BootStatus OK 'Test ingresso UEFI e filesystem superati.'
    $elfResult = $native::run_elf_tests()
    if ($elfResult -ne 0) { throw "Test ELF fallito: caso $elfResult" }
    Write-BootStatus OK 'Test ELF superati: profilo, limiti, troncamenti e 5000 mutazioni deterministiche.'
    $logResult = $native::run_log_tests()
    if ($logResult -ne 0) { throw "Test console fallito: caso $logResult" }
    Write-BootStatus OK 'Test colori, ripristino attributi e gestione errori console superati.'
    $kernelResult = $native::run_kernel_tests()
    if ($kernelResult -ne 0) { throw "Test lettura kernel fallito: caso $kernelResult" }
    Write-BootStatus OK 'Test lettura completa kernel, limiti e rilascio risorse superati.'

    $loadResult = $native::run_load_tests()
    if ($loadResult -ne 0) { throw "Test segmenti fallito: caso $loadResult" }
    Write-BootStatus OK 'Test copia segmenti, BSS e rollback superati.'

    $handoffResult = $native::run_handoff_tests()
    if ($handoffResult -ne 0) { throw "Test contratto segmenti fallito: caso $handoffResult" }
    Write-BootStatus OK 'Test contratto segmenti e invalidazione dei dati superati.'

    $mapResult = $native::run_map_tests()
    if ($mapResult -ne 0) { throw "Test mappa memoria fallito: caso $mapResult" }
    Write-BootStatus OK 'Test mappa memoria, crescita limitata e proprieta buffer superati.'

    $regionsResult = $native::run_region_tests()
    if ($regionsResult -ne 0) { throw "Test regioni memoria fallito: caso $regionsResult" }
    Write-BootStatus OK 'Test classificazione conservativa regioni memoria superati.'

    $stackResult = $native::run_stack_tests()
    if ($stackResult -ne 0) { throw "Test stack fallito: caso $stackResult" }
    $resourceResult = $native::run_resource_tests()
    if ($resourceResult -ne 0) { throw "Test copertura risorse fallito: caso $resourceResult" }
    Write-BootStatus OK 'Test stack, copertura, buchi e risorse distinte superati.'

    if ($native::run_plan_tests() -ne 0) { throw "Test piano mappature fallito." }
    Write-BootStatus OK 'Test piano mappature, guardia stack e permessi superati.'

    $tableResult = $native::run_table_tests()
    if ($tableResult -ne 0) { throw "Test tabelle pagina fallito: caso $tableResult" }
    Write-BootStatus OK 'Test tabelle, permessi, confini e rollback superati.'

    $contentsResult = $native::run_table_contents_tests()
    if ($contentsResult -ne 0) { throw "Test contenuto tabelle fallito: caso $contentsResult" }
    Write-BootStatus OK 'Contenuto tabelle verificato: permessi, foglie, guardia, cicli e indirizzi estranei.'
    $cpuResult = $native::run_cpu_tests()
    if ($cpuResult -ne 0) { throw "Test CPUID fallito: caso $cpuResult" }
    Write-BootStatus OK 'Test CPUID: leaf disponibili, risultati e invalidazione verificati.'

    if ($native::run_preparation_tests() -ne 0) { throw "Test copertura tabelle fallito." }
    Write-BootStatus OK 'Test copertura pagine delle tabelle, limiti e regioni escluse superati.'

    if ($native::run_paging_state_tests() -ne 0 -or $native::run_hex_log_tests() -ne 0) { throw "Test stato CR3 o log esadecimale fallito." }
    Write-BootStatus OK 'Test CR3, modo attivo, invalidazione e formato esadecimale superati.'

    # ====== verifica indipendente Rust delle allocazioni C ====== #

    & (Join-Path $PSScriptRoot 'test-rust.ps1') -ToolchainBin $ToolchainBin

    # ====== verifica immagine PE ====== #

    $bytes = [IO.File]::ReadAllBytes((Join-Path $esp 'BOOTX64.EFI'))
    Write-BootSection 'verifica immagine EFI'
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or
        [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664 -or
        [BitConverter]::ToUInt16($bytes, $pe + 24) -ne 0x20b -or
        [BitConverter]::ToUInt16($bytes, $pe + 24 + 68) -ne 10 -or
        [BitConverter]::ToUInt32($bytes, $pe + 24 + 16) -eq 0 -or
        [BitConverter]::ToUInt32($bytes, $pe + 24 + 120) -ne 0 -or
        [BitConverter]::ToUInt32($bytes, $pe + 24 + 156) -eq 0) {
        throw 'Immagine non conforme: controllare formato, entry point, import e rilocazioni.'
    }
    Write-BootStatus OK "Immagine verificata: $esp\BOOTX64.EFI ($($bytes.Length) byte)"
    Write-BootStatus WARN 'Le tabelle restano inattive; il salto al kernel non e ancora implementato.'
} catch {
    Write-BootStatus FAIL $_.Exception.Message
    throw
}















