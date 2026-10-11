param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/lapic-asan-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem ($sdk + '/Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
$savedPath = $env:PATH

# ============ checked tools and evidence ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    $messages | Tee-Object -FilePath "$OutputDirectory/tools.log" -Append
    if ($code) { throw "Tool failed: $Program ($code)" }
}

try {
    $bin = $vc.FullName + '/bin/Hostx64/x64'
    $env:PATH = $bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $sources = @('arch/x86/kernel/timer/lapic/calibration.c',
        'arch/x86/kernel/timer/lapic/driver.c', 'arch/x86/kernel/timer/lapic/runtime/runtime.c',
        'test/kernel/timer/lapic/timer_test.c', 'test/kernel/timer/lapic/platform_mock.c',
        'lib/memory/address_ranges.c')
    $inputs = @(Get-ChildItem (Join-Path $project 'arch/x86/kernel/timer/lapic') -Recurse -File |
        Where-Object Extension -In '.c','.h','.rs','.asm') +
        @(Get-ChildItem (Join-Path $project 'test/kernel/timer/lapic') -Recurse -File |
        Where-Object Extension -In '.c','.h','.rs','.ps1')
    $manifest = $inputs | ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 }
    $manifest | ConvertTo-Json | Out-File "$OutputDirectory/source-manifest.json" -Encoding utf8
    $objects = @()
    foreach ($source in $sources) {
        $object = Join-Path $OutputDirectory ($source.Replace('/', '-') + '.obj')
        Invoke-Checked ($bin + '/cl.exe') @('/nologo','/TC','/std:c11','/W4','/WX','/O2',
            '/fsanitize=address','/Zi','/MD','/c',('/I'+$project),
            (Join-Path $project $source),('/Fo'+$object),('/Fd'+$OutputDirectory+'/asan.pdb'))
        $objects += $object
    }
    Invoke-Checked (Get-Command rustc).Source @('--edition=2024','--crate-type=lib','--emit=obj',
        '-C','panic=abort','-C','opt-level=2','-D','warnings',
        (Join-Path $project 'arch/x86/kernel/timer/lapic/rust/calibration/validation.rs'),
        '-o',"$OutputDirectory/validation.obj")
    Invoke-Checked ($bin + '/ml64.exe') @('/nologo','/W3','/WX','/c',
        ('/Fo'+$OutputDirectory+'/irq-sleep.obj'),
        (Join-Path $project 'arch/x86/kernel/interrupts/registry/assembly/wait/bounded.asm'))
    $runner = @'
#include <stdio.h>
int run_lapic_timer_tests(void);
int run_irq_sleep_host_tests(void);
int main(void) {
    unsigned i;
    for (i = 0; i < 10; ++i) {
        int result = run_lapic_timer_tests();
        if (result) { printf("Timer test failed: %d\n", result); return 1; }
        result = run_irq_sleep_host_tests();
        if (result) { printf("IRQ rejection failed: %d\n", result); return 1; }
    }
    puts("LAPIC_ASAN_PASS: 10 repetitions, 10000 event cycles");
    return 0;
}
'@
    [IO.File]::WriteAllText("$OutputDirectory/runner.c",$runner,[Text.UTF8Encoding]::new($false))
    $arguments = @('/nologo','/std:c11','/W4','/WX','/fsanitize=address','/Zi','/MD',
        "/Tc$OutputDirectory/runner.c","/Fo$OutputDirectory/runner.obj",
        "/Fd$OutputDirectory/asan.pdb","/Fe$OutputDirectory/tests.exe") + $objects +
        @("$OutputDirectory/validation.obj","$OutputDirectory/irq-sleep.obj",'/link','/incremental:no')
    Invoke-Checked ($bin + '/cl.exe') $arguments
    Copy-Item ($bin + '/clang_rt.asan_dynamic-x86_64.dll') $OutputDirectory
    Invoke-Checked "$OutputDirectory/tests.exe" @()
    foreach ($source in $manifest) {
        if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Hash) {
            throw "Source changed during test: $($source.Path)"
        }
    }
    Write-Output "Evidence: $OutputDirectory"
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
    $env:PATH = $savedPath
}
