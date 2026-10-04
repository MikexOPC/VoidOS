param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ====== probe isolato con backend host senza accessi hardware ===== #

foreach ($source in @('arch/x86/kernel/interrupts/apic/probe.c','arch/x86/kernel/interrupts/apic/profile.c',
    'arch/x86/kernel/interrupts/apic/control/masked.c','arch/x86/kernel/interrupts/return/table.c',
    'arch/x86/kernel/serial/interrupts/driver.c','arch/x86/kernel/serial/transmit/buffer.c',
    'arch/x86/mm/devices/lapic.c','kernel/test/mm/devices/lapic_test.c',
    'kernel/test/interrupts/apic/control_test.c','kernel/test/interrupts/return/table_test.c',
    'kernel/firmware/acpi/madt/records.c','kernel/mm/inventory.c','lib/memory/address_ranges.c','kernel/test/interrupts/apic/probe_test.c')) {
    $object = Join-Path $OutputDirectory ('probe-'+($source.Replace('/','-') -replace '\.c$','.obj'))
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I'+$project) `
        ('/I'+(Join-Path $ToolchainBin '../../../include')) ('/I'+$SdkInclude) (Join-Path $project $source) ('/Fo'+$object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test probe MMIO fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/registers/validation.rs') -o ($OutputDirectory+'/apic-registers.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione validatori registri APIC fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/interrupts/return/rust/frame/validation.rs') -o ($OutputDirectory+'/irq-frame.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione validatori IRQ fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/interrupts/apic/control/rust/encoding/validation.rs') -o ($OutputDirectory+'/irq-control.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione encoding controller fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/mm/devices/rust/encoding/validation.rs') -o ($OutputDirectory+'/lapic-encoding.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione encoding LAPIC fallita.' }
$dll = Join-Path $OutputDirectory 'apic-probe-tests.dll'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/serial/interrupts/rust/routing/validation.rs') -o ($OutputDirectory+'/uart-routing.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione routing UART fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/serial/transmit/rust/bounds/validation.rs') -o ($OutputDirectory+'/uart-tx-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione bounds TX fallita.' }
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) @objects `
    ($OutputDirectory+'/apic-registers.obj') ($OutputDirectory+'/apic-profile.obj') ($OutputDirectory+'/madt-records.obj') `
    ($OutputDirectory+'/acpi-root-layout.obj') ($OutputDirectory+'/validation.obj') `
    ($OutputDirectory+'/irq-frame.obj') ($OutputDirectory+'/irq-control.obj') ($OutputDirectory+'/exception-frame.obj') ($OutputDirectory+'/uart-routing.obj') ($OutputDirectory+'/lapic-encoding.obj') ($OutputDirectory+'/uart-tx-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test probe MMIO fallito.' }
$typeName = 'ApicProbe'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { '+
    '[DllImport(@"'+$dll+'")] public static extern int run_apic_probe_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_apic_control_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_irq_table_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_uart_irq_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_uart_tx_irq_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_lapic_mapping_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
foreach ($method in @('run_apic_probe_tests','run_apic_control_tests','run_irq_table_tests','run_uart_irq_tests','run_uart_tx_irq_tests','run_lapic_mapping_tests')) {
    $result = $type.GetMethod($method).Invoke($null,@())
    if ($result -ne 0) { throw "Test $method fallito: $result" }
}
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/registers/validation_test.rs') -o ($OutputDirectory+'/apic-register-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test registri APIC Rust fallita.' }
& ($OutputDirectory+'/apic-register-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test registri APIC Rust falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $project 'kernel/test/interrupts/return/rust/frame/validation_test.rs') -o ($OutputDirectory+'/irq-frame-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test IRQ Rust fallita.' }
& ($OutputDirectory+'/irq-frame-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test IRQ Rust falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/control/encoding_test.rs') -o ($OutputDirectory+'/irq-control-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test encoding controller fallita.' }
& ($OutputDirectory+'/irq-control-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test encoding controller falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $project 'kernel/test/serial/rust/routing/validation_test.rs') -o ($OutputDirectory+'/uart-routing-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test routing UART fallita.' }
& ($OutputDirectory+'/uart-routing-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test routing UART falliti.' }
& $rustc --edition=2024 --test -D warnings (Join-Path $project 'kernel/test/mm/devices/rust/encoding/validation_test.rs') -o ($OutputDirectory+'/lapic-encoding-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test encoding LAPIC fallita.' }
& ($OutputDirectory+'/lapic-encoding-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test encoding LAPIC falliti.' }
