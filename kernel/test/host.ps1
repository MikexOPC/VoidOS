param([string]$ToolchainBin)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
if (!$ToolchainBin) {
    $version = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin/Hostx64/x64'
}
$output = Join-Path $project ('kernel/build/host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
Write-BootSection 'test kernel contratto memoria e diagnostica'
$objects = @()
$sdk = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Header UCRT del Windows SDK assenti.' }
foreach ($source in @('kernel/test/contract_test.c','kernel/test/platform_mock.c','kernel/boot/contract.c',
    'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/test/mm/physical/allocator_test.c',
    'kernel/test/mm/physical/alignment_test.c',
    'arch/x86/mm/regions/mapping.c','kernel/test/mm/regions/mapping_test.c',
    'arch/x86/mm/arena/objects.c','kernel/test/mm/arena/objects_test.c',
    'arch/x86/mm/paging/inventory.c','kernel/test/mm/paging/inventory_test.c',
    'lib/firmware/acpi/rsdp.c',
    'kernel/firmware/acpi/root.c','kernel/test/firmware/acpi/root_test.c',
    'kernel/firmware/acpi/madt/parser.c','kernel/firmware/acpi/madt/records.c','kernel/test/firmware/acpi/madt/parser_test.c',
    'arch/x86/kernel/interrupts/apic/profile.c','kernel/test/interrupts/apic/profile_test.c',
    'kernel/diagnostics/log.c','arch/x86/kernel/serial.c','arch/x86/mm/window/page.c','kernel/test/mm/window/page_test.c',
    'kernel/mm/objects/pool.c','kernel/test/mm/objects/pool_test.c',
    'arch/x86/kernel/exceptions/terminal.c','kernel/test/exceptions/terminal_test.c',
    'arch/x86/kernel/descriptors/stacks.c','arch/x86/kernel/descriptors/segments.c','kernel/test/descriptors/stacks_test.c',
    'kernel/test/descriptors/segments_test.c','arch/x86/kernel/stack/main.c','kernel/test/stack/main_test.c',
    'arch/x86/lib/descriptors/gdt.c','arch/x86/lib/descriptors/tss.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I' + $project) ('/I' + (Join-Path $ToolchainBin '../../../include')) ('/I' + (Join-Path $sdk.FullName 'ucrt')) (Join-Path $project $source) ('/Fo' + $object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test C fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'lib/firmware/acpi/rust/rsdp/validation.rs') -o ($output + '/acpi-rsdp-fields.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione campi ACPI fallita.' }
$objects += ($output + '/acpi-rsdp-fields.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/firmware/acpi/rust/root/layout.rs') -o ($output + '/acpi-root-layout.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione layout root ACPI fallita.' }
$objects += ($output + '/acpi-root-layout.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/firmware/acpi/madt/rust/records/validation.rs') -o ($output + '/madt-records.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione record MADT fallita.' }
$objects += ($output + '/madt-records.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/profile/validation.rs') -o ($output + '/apic-profile.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione profilo APIC fallita.' }
$objects += ($output + '/apic-profile.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/mm/paging/rust/validation/entries.rs') -o ($output + '/paging-entries.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione formato paging fallita.' }
$objects += ($output + '/paging-entries.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/mm/regions/rust/layout/validation.rs') -o ($output + '/region-layout.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione layout mapping fallita.' }
$objects += ($output + '/region-layout.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/mm/physical/rust/alignment/placement.rs') -o ($output + '/physical-alignment.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione allineamento fisico fallita.' }
$objects += ($output + '/physical-alignment.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/kernel/descriptors/rust/layout/validation.rs') -o ($output + '/ist-layout.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione layout IST fallita.' }
$objects += ($output + '/ist-layout.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/kernel/exceptions/rust/frame/validation.rs') -o ($output + '/exception-frame.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione frame eccezioni fallita.' }
$objects += ($output + '/exception-frame.obj')
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/mm/rust/regions/validation.rs') -o ($output + '/validation.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione Rust fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'arch/x86/mm/window/rust/layout/validation.rs') -o ($output + '/window-layout.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione layout Rust fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/mm/objects/rust/validation/bounds.rs') -o ($output + '/object-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione bounds oggetti Rust fallita.' }
$dll = Join-Path $output 'kernel-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /machine:x64 /dll /noentry /nodefaultlib /incremental:no ('/out:' + $dll) ($output + '/validation.obj') ($output + '/window-layout.obj') ($output + '/object-bounds.obj') @objects
if ($LASTEXITCODE -ne 0) { throw 'Link test fallito.' }
$typeName = 'KernelTests' + [guid]::NewGuid().ToString('N')
$interop = 'using System.Runtime.InteropServices; public static class ' + $typeName + ' { [DllImport(@"' + $dll + '")] public static extern int run_kernel_tests(); }'
$type = Add-Type -TypeDefinition $interop -PassThru
$result = $type.GetMethod('run_kernel_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test kernel fallito: $result" }
$acpiSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Acpi { [DllImport(@"' + $dll + '")] public static extern int run_acpi_root_tests(); }'
$acpiType = Add-Type -TypeDefinition $acpiSource -PassThru
$result = $acpiType.GetMethod('run_acpi_root_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test root ACPI fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'firmware/acpi/rust/root/layout_test.rs') -o ($output + '/acpi-root-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test root ACPI Rust fallita.' }
& ($output + '/acpi-root-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test root ACPI Rust falliti.' }
Write-BootStatus OK 'Root ACPI: checksum, capacita, duplicati, errori lettore e 2 test Rust verificati.'
$madtSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Madt { [DllImport(@"' + $dll + '")] public static extern int run_madt_tests(); }'
$madtType = Add-Type -TypeDefinition $madtSource -PassThru
$result = $madtType.GetMethod('run_madt_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test MADT fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'firmware/acpi/madt/rust/records/validation_test.rs') -o ($output + '/madt-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test MADT Rust fallita.' }
& ($output + '/madt-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test MADT Rust falliti.' }
Write-BootStatus OK 'MADT: streaming, checksum, record APIC, NMI e 4 test Rust verificati.'
$apicSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Apic { [DllImport(@"' + $dll + '")] public static extern int run_apic_profile_tests(); }'
$apicType = Add-Type -TypeDefinition $apicSource -PassThru
$result = $apicType.GetMethod('run_apic_profile_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test profilo APIC fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'interrupts/apic/rust/profile/validation_test.rs') -o ($output + '/apic-profile-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test APIC Rust fallita.' }
& ($output + '/apic-profile-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test APIC Rust falliti.' }
Write-BootStatus OK 'Preflight APIC: BSP, MADT, PAT corrente e 3 test Rust verificati.'
& (Join-Path $PSScriptRoot 'interrupts/apic/scripts/host-probe.ps1') -OutputDirectory $output -ToolchainBin $ToolchainBin -SdkInclude (Join-Path $sdk.FullName 'ucrt')
Write-BootStatus OK 'MMIO APIC, IRQ e COM1: revoca PTE, maschere, NMI, ACK/EOI, rollback IDT e 9 test Rust verificati.'
$typeSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Physical { [DllImport(@"' + $dll + '")] public static extern int run_physical_tests(); }'
$physicalType = Add-Type -TypeDefinition $typeSource -PassThru
$result = $physicalType.GetMethod('run_physical_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test allocatore fisico fallito: $result" }
$alignmentSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Alignment { [DllImport(@"' + $dll + '")] public static extern int run_alignment_tests(); }'
$alignmentType = Add-Type -TypeDefinition $alignmentSource -PassThru
$result = $alignmentType.GetMethod('run_alignment_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test allineamento fisico fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'mm/physical/rust/alignment/placement_test.rs') -o ($output + '/physical-alignment-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test allineamento fisico fallita.' }
& ($output + '/physical-alignment-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test allineamento fisico Rust falliti.' }
Write-BootStatus OK 'Allineamento fisico: split, coalescenza, 8192 operazioni e 278528 confronti Rust verificati.'
$regionSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Regions { [DllImport(@"' + $dll + '")] public static extern int run_region_tests(); }'
$regionType = Add-Type -TypeDefinition $regionSource -PassThru
$result = $regionType.GetMethod('run_region_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test mapping simultanei fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'mm/regions/rust/layout/validation_test.rs') -o ($output + '/region-layout-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test mapping Rust fallita.' }
& ($output + '/region-layout-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test mapping Rust falliti.' }
Write-BootStatus OK 'Mapping simultanei: pin, rollback, stale handle, guardie, 8640 corruzioni PTE e 128 cicli verificati.'
$arenaSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Arena { [DllImport(@"' + $dll + '")] public static extern int run_arena_tests(); }'
$arenaType = Add-Type -TypeDefinition $arenaSource -PassThru
$result = $arenaType.GetMethod('run_arena_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test arena persistente fallito: $result" }
Write-BootStatus OK 'Arena oggetti: binding mapping, lifetime, OOM, 64 record e 256 cicli verificati.'
$pagingSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Paging { [DllImport(@"' + $dll + '")] public static extern int run_paging_inventory_tests(); }'
$pagingType = Add-Type -TypeDefinition $pagingSource -PassThru
$result = $pagingType.GetMethod('run_paging_inventory_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test inventario paging fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'mm/paging/rust/validation/entries_test.rs') -o ($output + '/paging-entries-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test formato paging fallita.' }
& ($output + '/paging-entries-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test formato paging Rust falliti.' }
Write-BootStatus OK 'Inventario RAM/MMIO: alias, riserve, owner, 128 tabelle e 25152 alterazioni dei metadati verificati.'
$windowSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Window { [DllImport(@"' + $dll + '")] public static extern int run_window_tests(); }'
$windowType = Add-Type -TypeDefinition $windowSource -PassThru
$result = $windowType.GetMethod('run_window_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test mapper pagina fallito: $result" }
$objectSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Objects { [DllImport(@"' + $dll + '")] public static extern int run_object_tests(); }'
$objectType = Add-Type -TypeDefinition $objectSource -PassThru
$result = $objectType.GetMethod('run_object_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test pool oggetti fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'rust/regions/validation_test.rs') -o ($output + '/regions-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test Rust fallita.' }
& ($output + '/regions-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test Rust falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'mm/window/rust/layout/validation_test.rs') -o ($output + '/window-layout-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test layout Rust fallita.' }
& ($output + '/window-layout-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test layout Rust falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'mm/objects/rust/validation/bounds_test.rs') -o ($output + '/object-bounds-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test bounds oggetti fallita.' }
& ($output + '/object-bounds-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test bounds oggetti Rust falliti.' }
Write-BootStatus OK 'Pool oggetti: ownership, allineamento, OOM e 1024 operazioni verificati; regressione kernel e 11 test Rust superati.'
$exceptionSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Exceptions { [DllImport(@"' + $dll + '")] public static extern int run_exception_tests(); }'
$exceptionType = Add-Type -TypeDefinition $exceptionSource -PassThru
$result = $exceptionType.GetMethod('run_exception_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test IDT fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'exceptions/rust/frame/validation_test.rs') -o ($output + '/exception-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test frame fallita.' }
& ($output + '/exception-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test frame Rust falliti.' }
Write-BootStatus OK 'IDT: 4176 alterazioni di bit, bounds, attivazione e rollback verificati; 4 test Rust del frame superati.'
$descriptorSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Descriptors { [DllImport(@"' + $dll + '")] public static extern int run_stack_tests(); [DllImport(@"' + $dll + '")] public static extern int run_segment_tests(); [DllImport(@"' + $dll + '")] public static extern int run_main_stack_tests(); }'
$descriptorType = Add-Type -TypeDefinition $descriptorSource -PassThru
foreach ($method in @('run_stack_tests','run_segment_tests','run_main_stack_tests')) {
    $result = $descriptorType.GetMethod($method).Invoke($null, @())
    if ($result -ne 0) { throw "Test descrittori fallito: $method, $result" }
}
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'descriptors/rust/layout/validation_test.rs') -o ($output + '/ist-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test IST fallita.' }
& ($output + '/ist-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test IST Rust falliti.' }
& (Join-Path $PSScriptRoot 'serial/transmit/scripts/host.ps1') -OutputDirectory $output -ToolchainBin $ToolchainBin -SdkInclude (Join-Path $sdk.FullName 'ucrt')
Write-BootStatus OK 'GDT TSS stack e TX bounded: ownership, rollback, revoca e corruzioni verificati; 48 test Rust complessivi.'
