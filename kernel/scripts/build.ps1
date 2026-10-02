param([string]$ToolchainBin, [switch]$TestPanic, [string]$OutputDirectory, [switch]$Install,
    [ValidateSet('none','execute','unmapped')][string]$TestWindowFault = 'none')
$ErrorActionPreference = 'Stop'
if ($Install -and ($TestPanic -or $TestWindowFault -ne 'none')) { throw 'Non installare immagini con fault di test nella ESP ordinaria.' }
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
if (!$ToolchainBin) {
    $version = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $ToolchainBin = Join-Path $version.FullName 'bin/Hostx64/x64'
}
$output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $project 'kernel/build' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
function Invoke-Compiler([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita: $program" }
}
Write-BootSection 'kernel autonomo C e Rust'
$flags = @('/nologo','/TC','/std:c11','/W4','/WX','/GS-','/Zl','/O2','/c',('/I' + $project))
if ($TestPanic) { $flags += '/DKERNEL_TEST_PANIC=1' }
if ($TestWindowFault -ne 'none') {
    $mode = if ($TestWindowFault -eq 'execute') {1} else {2}
    $flags += ('/DKERNEL_TEST_WINDOW_FAULT=' + $mode)
}
$objects = @()
foreach ($source in @('arch/x86/kernel/entry.c','arch/x86/kernel/serial.c','arch/x86/mm/window/page.c','arch/x86/mm/window/access.c','kernel/main.c',
    'kernel/boot/contract.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/diagnostics/log.c')) {
    $object = Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags + @((Join-Path $project $source),('/Fo' + $object)))
    $objects += $object
}
Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/io.obj'),
    (Join-Path $project 'arch/x86/kernel/assembly/platform/io.asm'))
if ($TestWindowFault -ne 'none') {
    Invoke-Compiler (Join-Path $ToolchainBin 'ml64.exe') @('/nologo','/W3','/WX','/c',('/Fo' + $output + '/fault.obj'),
        (Join-Path $project 'kernel/test/mm/window/assembly/fault/probe.asm'))
    $objects += ($output + '/fault.obj')
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','--target','x86_64-pc-windows-msvc',
    '-C','panic=abort','-C','opt-level=2','-D','warnings',(Join-Path $project 'kernel/mm/rust/regions/validation.rs'),
    '-o',($output + '/validation.obj'))
Invoke-Compiler (Join-Path $ToolchainBin 'link.exe') (@('/nologo','/machine:x64','/subsystem:native',
    '/entry:kernel_entry','/nodefaultlib','/incremental:no','/dynamicbase:no','/fixed','/base:0x100000000',
    ('/out:' + $output + '/kernel.exe'),($output + '/io.obj'),($output + '/validation.obj')) + $objects)
& (Join-Path $PSScriptRoot 'convert-image.ps1') -Image ($output + '/kernel.exe') -Output ($output + '/kernel.elf')
Write-BootStatus OK "Kernel ELF verificato e generato: $output/kernel.elf"
if ($Install) {
    $esp = Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT'
    if (!(Test-Path -LiteralPath (Join-Path $esp 'BOOTX64.EFI'))) { throw 'Prima costruire il bootloader con -BootKernel.' }
    Copy-Item -LiteralPath (Join-Path $output 'kernel.elf') -Destination (Join-Path $esp 'kernel.elf')
    Write-BootStatus OK 'Kernel installato nella ESP generata; usare un bootloader costruito con -BootKernel.'
}
