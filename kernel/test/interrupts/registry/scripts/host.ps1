param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ============ lifecycle degli handler IRQ senza accessi privilegiati ============ #

foreach ($source in @('arch/x86/kernel/interrupts/registry/registry.c',
    'kernel/test/interrupts/registry/registry_test.c','lib/memory/address_ranges.c')) {
    $object = Join-Path $OutputDirectory ('registry-'+($source.Replace('/','-') -replace '\.c$','.obj'))
    & (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O2 /c ('/I'+$project) `
        ('/I'+(Join-Path $ToolchainBin '../../../include')) ('/I'+$SdkInclude) (Join-Path $project $source) ('/Fo'+$object)
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione test registro IRQ fallita.' }
    $objects += $object
}
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
& $rustc --edition=2024 --crate-type=lib --emit=obj -C panic=abort -C opt-level=2 -D warnings `
    (Join-Path $project 'arch/x86/kernel/interrupts/registry/rust/subscription/validation.rs') -o ($OutputDirectory+'/irq-registry-state.obj')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione subscription IRQ fallita.' }
$dll = Join-Path $OutputDirectory 'irq-registry-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) `
    @objects ($OutputDirectory+'/irq-registry-state.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test registro IRQ fallito.' }
$typeName = 'IrqRegistry'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { '+
    '[DllImport(@"'+$dll+'")] public static extern int run_irq_registry_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
$result = $type.GetMethod('run_irq_registry_tests').Invoke($null,@())
if ($result -ne 0) { throw "Test registro IRQ fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/subscription/validation_test.rs') -o ($OutputDirectory+'/irq-registry-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test subscription IRQ Rust fallita.' }
& ($OutputDirectory+'/irq-registry-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test subscription IRQ Rust fallito.' }
