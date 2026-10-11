param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ====== buffer TX con UART simulata senza accessi privilegiati ===== #

foreach ($source in @('arch/x86/kernel/serial/transmit/buffer.c',
    'test/kernel/serial/transmit/buffer_test.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $OutputDirectory ('tx-'+($source.Replace('/','-') -replace '\.c$','.obj'))
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I'+$project) `
        ('/I'+(Join-Path $ToolchainBin '../../../include')) ('/I'+$SdkInclude) (Join-Path $project $source) ('/Fo'+$object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test buffer TX fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/serial/transmit/rust/bounds/validation.rs') -o ($OutputDirectory+'/uart-tx-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione bounds TX fallita.' }
$dll = Join-Path $OutputDirectory 'uart-tx-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) `
    @objects ($OutputDirectory+'/uart-tx-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test TX fallito.' }
$typeName = 'UartTx'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { '+
    '[DllImport(@"'+$dll+'")] public static extern int run_uart_tx_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
$result = $type.GetMethod('run_uart_tx_tests').Invoke($null,@())
if ($result -ne 0) { throw "Test buffer TX fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/bounds/validation_test.rs') -o ($OutputDirectory+'/uart-tx-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test bounds TX Rust fallita.' }
& ($OutputDirectory+'/uart-tx-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test bounds TX Rust fallito.' }
