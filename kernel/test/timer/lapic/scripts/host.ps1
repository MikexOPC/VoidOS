param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ============ timer LAPIC senza ipotesi di frequenza senza accessi privilegiati ============ #

foreach ($source in @('arch/x86/kernel/timer/lapic/calibration.c','arch/x86/kernel/timer/lapic/driver.c',
    'kernel/test/timer/lapic/timer_test.c','kernel/test/timer/lapic/platform_mock.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $OutputDirectory ('timer-'+($source.Replace('/','-') -replace '\.c$','.obj'))
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I'+$project) `
        ('/I'+(Join-Path $ToolchainBin '../../../include')) ('/I'+$SdkInclude) (Join-Path $project $source) ('/Fo'+$object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test timer LAPIC fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/timer/lapic/rust/calibration/validation.rs') -o ($OutputDirectory+'/lapic-timer-state.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione calibrazione timer fallita.' }
$dll = Join-Path $OutputDirectory 'lapic-timer-tests.dll'
& (Join-Path $ToolchainBin 'ml64.exe') /nologo /W3 /WX /c ('/Fo'+$OutputDirectory+'/irq-sleep.obj') `
    (Join-Path $project 'arch/x86/kernel/interrupts/registry/assembly/wait/bounded.asm')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione attesa IRQ fallita.' }
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) `
    @objects ($OutputDirectory+'/lapic-timer-state.obj') ($OutputDirectory+'/irq-sleep.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test timer LAPIC fallito.' }
$typeName = 'LapicTimer'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { '+
    '[DllImport(@"'+$dll+'")] public static extern int run_lapic_timer_tests(); '+
    '[DllImport(@"'+$dll+'")] public static extern int run_irq_sleep_host_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
$result = $type.GetMethod('run_lapic_timer_tests').Invoke($null,@())
if ($result -ne 0) { throw "Test timer LAPIC fallito: $result" }
$result = $type.GetMethod('run_irq_sleep_host_tests').Invoke($null,@())
if ($result -ne 0) { throw "Attesa IRQ non rifiutata in ambiente host: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/calibration/validation_test.rs') -o ($OutputDirectory+'/lapic-timer-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test calibrazione timer Rust fallita.' }
& ($OutputDirectory+'/lapic-timer-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test calibrazione timer Rust fallito.' }
