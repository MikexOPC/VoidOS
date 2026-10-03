param([string]$OutputDirectory,[string]$ToolchainBin,[string]$SdkInclude)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$objects = @()

# ====== probe isolato con backend host senza accessi hardware ===== #

foreach ($source in @('arch/x86/kernel/interrupts/apic/probe.c','arch/x86/kernel/interrupts/apic/profile.c',
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
$dll = Join-Path $OutputDirectory 'apic-probe-tests.dll'
& (Join-Path $ToolchainBin 'link.exe') /nologo /dll /noentry /nodefaultlib /incremental:no ('/out:'+$dll) @objects `
    ($OutputDirectory+'/apic-registers.obj') ($OutputDirectory+'/apic-profile.obj') ($OutputDirectory+'/madt-records.obj') `
    ($OutputDirectory+'/acpi-root-layout.obj') ($OutputDirectory+'/validation.obj')
if ($LASTEXITCODE -ne 0) { throw 'Link test probe MMIO fallito.' }
$typeName = 'ApicProbe'+[guid]::NewGuid().ToString('N')
$source = 'using System.Runtime.InteropServices; public static class '+$typeName+' { [DllImport(@"'+$dll+'")] public static extern int run_apic_probe_tests(); }'
$type = Add-Type -TypeDefinition $source -PassThru
$result = $type.GetMethod('run_apic_probe_tests').Invoke($null,@())
if ($result -ne 0) { throw "Test probe MMIO fallito: $result" }
& $rustc --edition=2024 --test -D warnings (Join-Path $PSScriptRoot '../rust/registers/validation_test.rs') -o ($OutputDirectory+'/apic-register-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compilazione test registri APIC Rust fallita.' }
& ($OutputDirectory+'/apic-register-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test registri APIC Rust falliti.' }
