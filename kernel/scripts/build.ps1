param([string]$ToolchainBin, [switch]$TestPanic, [string]$OutputDirectory, [switch]$Install,
    [ValidateSet('none','execute','unmapped','guard-low','guard-high')][string]$TestWindowFault = 'none',
    [ValidateSet('none','opcode','selector','divide','stack','nested','double')][string]$TestCpuFault = 'none',
    [ValidateSet('none','retired','guard-high','execute','return')][string]$TestMainFault = 'none',
    [ValidateSet('none','retired','guard-low','guard-high','execute')][string]$TestRegionFault = 'none')
$ErrorActionPreference = 'Stop'
if ($Install -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none' -or $TestRegionFault -ne 'none')) { throw 'Non installare immagini con fault di test nella ESP ordinaria.' }
if ($TestRegionFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none' -or $TestMainFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($TestMainFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none' -or $TestCpuFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
if ($TestCpuFault -ne 'none' -and ($TestPanic -or $TestWindowFault -ne 'none')) { throw 'Scegliere un solo fault di test.' }
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
if (!$ToolchainBin) {
    $version = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin/Hostx64/x64'
}
$output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $project 'kernel/build' }
New-Item -ItemType Directory -Force -Path $output | Out-Null

# ====== ogni compilazione deve terminare senza errori ===== #

function Invoke-Compiler([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita: $program" }
}
Write-BootSection 'kernel autonomo C e Rust'
$sdk = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Header UCRT del Windows SDK assenti.' }
$flags = @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/O2','/c',('/I' + $project),
    ('/I' + (Join-Path $ToolchainBin '../../../include')),('/I' + (Join-Path $sdk.FullName 'ucrt')))
if ($TestPanic) { $flags += '/DKERNEL_TEST_PANIC=1' }
if ($TestRegionFault -ne 'none') {
    $mode = switch ($TestRegionFault) { 'retired' {1} 'guard-low' {2} 'guard-high' {3} 'execute' {4} }
    $flags += ('/DKERNEL_TEST_REGION_FAULT=' + $mode)
}
if ($TestMainFault -ne 'none') {
    $mode = switch ($TestMainFault) { 'retired' {1} 'guard-high' {2} 'execute' {3} 'return' {4} }
    $flags += ('/DKERNEL_TEST_MAIN_FAULT=' + $mode)
}
if ($TestWindowFault -ne 'none') {
    $mode = switch ($TestWindowFault) { 'execute' {1} 'unmapped' {2} 'guard-low' {3} 'guard-high' {4} }
    $flags += ('/DKERNEL_TEST_WINDOW_FAULT=' + $mode)
}
$objects = @()
if ($TestCpuFault -ne 'none') {
    $mode = switch ($TestCpuFault) { 'opcode' {1} 'selector' {2} 'divide' {3} 'stack' {4} 'nested' {1} 'double' {2} }
    $flags += ('/DKERNEL_TEST_CPU_FAULT=' + $mode)
    if ($TestCpuFault -eq 'nested') { $flags += '/DKERNEL_TEST_NESTED_FAULT=1' }
    if ($TestCpuFault -eq 'double') { $flags += '/DKERNEL_TEST_DOUBLE_FAULT=1' }
}
foreach ($source in @('arch/x86/kernel/entry.c','arch/x86/kernel/serial.c','arch/x86/mm/window/page.c','arch/x86/mm/window/access.c','kernel/main.c',
    'kernel/boot/contract.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/objects/pool.c','kernel/diagnostics/log.c',
    'arch/x86/mm/regions/mapping.c','arch/x86/mm/regions/access.c',
    'arch/x86/mm/arena/objects.c',
    'arch/x86/mm/paging/inventory.c','arch/x86/mm/paging/access.c',
    'lib/firmware/acpi/rsdp.c',
    'kernel/firmware/acpi/root.c',
    'kernel/firmware/acpi/madt/parser.c','kernel/firmware/acpi/madt/records.c',
    'arch/x86/kernel/interrupts/apic/profile.c',
    'arch/x86/kernel/interrupts/apic/probe.c','arch/x86/kernel/interrupts/apic/access.c',
    'arch/x86/kernel/interrupts/apic/control/masked.c','arch/x86/kernel/interrupts/apic/control/access.c',
    'arch/x86/kernel/interrupts/return/table.c',
    'arch/x86/kernel/serial/interrupts/driver.c','arch/x86/kernel/serial/transmit/buffer.c',
    'arch/x86/mm/devices/access.c','arch/x86/mm/devices/lapic.c',
    'arch/x86/kernel/exceptions/terminal.c','arch/x86/kernel/descriptors/stacks.c','arch/x86/kernel/descriptors/segments.c',
    'arch/x86/kernel/descriptors/stack_access.c','arch/x86/kernel/stack/main.c','arch/x86/lib/descriptors/gdt.c','arch/x86/lib/descriptors/tss.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $output ($source.Replace('/', '-') -replace '\.c$', '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
    $objects += $object
}
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/io.obj'),
    (Join-Path $project 'arch/x86/kernel/assembly/platform/io.asm'))
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/exception-entry.obj'),
    (Join-Path $project 'arch/x86/kernel/exceptions/assembly/entry/terminal.asm'))
$objects += ($output + '/exception-entry.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/segment-entry.obj'),
    (Join-Path $project 'arch/x86/kernel/descriptors/assembly/activation/segments.asm'))
$objects += ($output + '/segment-entry.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/stack-switch.obj'),
    (Join-Path $project 'arch/x86/kernel/stack/assembly/switch/main.asm'))
$objects += ($output + '/stack-switch.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/apic-capture.obj'),
    (Join-Path $project 'arch/x86/kernel/interrupts/apic/assembly/profile/capture.asm'))
$objects += ($output + '/apic-capture.obj')
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$output+'/uart-wait.obj'),
    (Join-Path $project 'arch/x86/kernel/serial/interrupts/assembly/wait/bounded.asm'))
$objects += ($output+'/uart-wait.obj')
foreach ($entry in @('entry/return','verification/registers')) {
    $object = $output + '/irq-' + ($entry.Replace('/','-')) + '.obj'
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo'+$object),
        (Join-Path $project ('arch/x86/kernel/interrupts/return/assembly/'+$entry+'.asm')))
    $objects += $object
}
if ($TestCpuFault -ne 'none') {
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/cpu-fault.obj'),
        (Join-Path $project 'kernel/test/exceptions/assembly/fault/probe.asm'))
    $objects += ($output + '/cpu-fault.obj')
}
if ($TestWindowFault -ne 'none') {
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/fault.obj'),
        (Join-Path $project 'kernel/test/mm/window/assembly/fault/probe.asm'))
    $objects += ($output + '/fault.obj')
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/devices/rust/encoding/validation.rs'),
    '-o',($output + '/lapic-encoding.obj'))
$objects += ($output + '/lapic-encoding.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'lib/firmware/acpi/rust/rsdp/validation.rs'),
    '-o',($output + '/acpi-rsdp-fields.obj'))
$objects += ($output + '/acpi-rsdp-fields.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/firmware/acpi/rust/root/layout.rs'),
    '-o',($output + '/acpi-root-layout.obj'))
$objects += ($output + '/acpi-root-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/firmware/acpi/madt/rust/records/validation.rs'),
    '-o',($output + '/madt-records.obj'))
$objects += ($output + '/madt-records.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/profile/validation.rs'),
    '-o',($output + '/apic-profile.obj'))
$objects += ($output + '/apic-profile.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/rust/registers/validation.rs'),
    '-o',($output + '/apic-registers.obj'))
$objects += ($output + '/apic-registers.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/return/rust/frame/validation.rs'),
    '-o',($output + '/irq-frame.obj'))
$objects += ($output + '/irq-frame.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/interrupts/apic/control/rust/encoding/validation.rs'),
    '-o',($output + '/irq-control.obj'))
$objects += ($output + '/irq-control.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/serial/interrupts/rust/routing/validation.rs'),
    '-o',($output + '/uart-routing.obj'))
$objects += ($output + '/uart-routing.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/serial/transmit/rust/bounds/validation.rs'),
    '-o',($output + '/uart-tx-bounds.obj'))
$objects += ($output + '/uart-tx-bounds.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/paging/rust/validation/entries.rs'),
    '-o',($output + '/paging-entries.obj'))
$objects += ($output + '/paging-entries.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/regions/rust/layout/validation.rs'),
    '-o',($output + '/region-layout.obj'))
$objects += ($output + '/region-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/physical/rust/alignment/placement.rs'),
    '-o',($output + '/physical-alignment.obj'))
$objects += ($output + '/physical-alignment.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/descriptors/rust/layout/validation.rs'),
    '-o',($output + '/ist-layout.obj'))
$objects += ($output + '/ist-layout.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/kernel/exceptions/rust/frame/validation.rs'),
    '-o',($output + '/exception-frame.obj'))
$objects += ($output + '/exception-frame.obj')
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/rust/regions/validation.rs'),
    '-o',($output + '/validation.obj'))
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'arch/x86/mm/window/rust/layout/validation.rs'),
    '-o',($output + '/window-layout.obj'))
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/objects/rust/validation/bounds.rs'),
    '-o',($output + '/object-bounds.obj'))
Invoke-Compiler (Join-Path $ToolchainBin 'link.exe') (@('/nologo','/machine:x64','/subsystem:native',
    '/entry:kernel_entry','/nodefaultlib','/incremental:no','/dynamicbase:no','/fixed','/base:0x100000000',
    ('/out:' + $output + '/kernel.exe'),($output + '/io.obj'),($output + '/validation.obj'),($output + '/window-layout.obj'),
    ($output + '/object-bounds.obj')) + $objects)
& (Join-Path $PSScriptRoot 'convert-image.ps1') -Image ($output + '/kernel.exe') -Output ($output + '/kernel.elf')
Write-BootStatus OK "Kernel ELF verificato e generato: $output/kernel.elf"
if ($Install) {
    $esp = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT'
    if (!(Test-Path -LiteralPath (Join-Path $esp 'BOOTX64.EFI'))) { throw 'Prima costruire il bootloader con -BootKernel.' }
    Copy-Item -LiteralPath (Join-Path $output 'kernel.elf') -Destination (Join-Path $esp 'kernel.elf')
    Write-BootStatus OK 'Kernel installato nella ESP generata; usare un bootloader costruito con -BootKernel.'
}
