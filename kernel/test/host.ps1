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
foreach ($source in @('kernel/test/contract_test.c','kernel/test/platform_mock.c','kernel/boot/contract.c',
    'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/test/mm/physical/allocator_test.c',
    'kernel/diagnostics/log.c','arch/x86/kernel/serial.c')) {
    $object = Join-Path $output ([IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I' + $project) (Join-Path $project $source) ('/Fo' + $object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test C fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj --target x86_64-pc-windows-msvc -C panic=abort -C opt-level=2 -D warnings (Join-Path $project 'kernel/mm/rust/regions/validation.rs') -o ($output + '/validation.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione Rust fallita.' }
$dll = Join-Path $output 'kernel-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /machine:x64 /dll /noentry /nodefaultlib /incremental:no ('/out:' + $dll) ($output + '/validation.obj') @objects
if ($LASTEXITCODE -ne 0) { throw 'Link test fallito.' }
$typeName = 'KernelTests' + [guid]::NewGuid().ToString('N')
$interop = 'using System.Runtime.InteropServices; public static class ' + $typeName + ' { [DllImport(@"' + $dll + '")] public static extern int run_kernel_tests(); }'
$type = Add-Type -TypeDefinition $interop -PassThru
$result = $type.GetMethod('run_kernel_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test kernel fallito: $result" }
$typeSource = 'using System.Runtime.InteropServices; public static class ' + $typeName + 'Physical { [DllImport(@"' + $dll + '")] public static extern int run_physical_tests(); }'
$physicalType = Add-Type -TypeDefinition $typeSource -PassThru
$result = $physicalType.GetMethod('run_physical_tests').Invoke($null, @())
if ($result -ne 0) { throw "Test allocatore fisico fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot 'rust/regions/validation_test.rs') -o ($output + '/regions-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test Rust fallita.' }
& ($output + '/regions-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test Rust falliti.' }
Write-BootStatus OK 'Contratto, inventario, allocatore con 1024 sequenze, UART e colori verificati; 4 test Rust superati.'
