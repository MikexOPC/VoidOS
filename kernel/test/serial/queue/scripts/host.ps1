param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ============ coda TX FIFO senza accessi privilegiati ============ #

foreach ($source in @('arch/x86/kernel/serial/queue/queue.c',
    'kernel/test/serial/queue/queue_test.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $OutputDirectory ('queue-'+($source.Replace('/','-') -replace '\.c$','.obj'))
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I'+$project) `
        ('/I'+(Join-Path $ToolchainBin '../../../include')) ('/I'+$SdkInclude) (Join-Path $project $source) ('/Fo'+$object)
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test coda TX fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/serial/transmit/rust/bounds/validation.rs') -o ($OutputDirectory+'/uart-tx-bounds.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione bounds TX fallita.' }
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/serial/queue/rust/state/validation.rs') -o ($OutputDirectory+'/uart-queue-state.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione stato coda TX fallita.' }
$dll = Join-Path $OutputDirectory 'uart-queue-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) `
    @objects ($OutputDirectory+'/uart-tx-bounds.obj') ($OutputDirectory+'/uart-queue-state.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test coda TX fallito.' }
$typeName = 'UartQueue'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { '+
    '[DllImport(@"'+$dll+'")] public static extern int run_uart_queue_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
$result = $type.GetMethod('run_uart_queue_tests').Invoke($null,@())
if ($result -ne 0) { throw "Test coda TX fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/state/validation_test.rs') -o ($OutputDirectory+'/uart-queue-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test stato coda TX Rust fallita.' }
& ($OutputDirectory+'/uart-queue-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test stato coda TX Rust fallito.' }
