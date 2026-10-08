param()
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../../../..')).Path
$out = Join-Path $project ('kernel/build/watchdog-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Get-ChildItem $module -Recurse -File | ForEach-Object { Get-FileHash $_.FullName } |
    ConvertTo-Json | Out-File "$out/source-manifest.json" -Encoding utf8
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory |
    Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE; $savedLib = $env:LIB; $savedPath = $env:PATH

# ============ checked tool execution ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    "Program: $Program" | Out-File "$out/tools.log" -Append -Encoding utf8
    $messages | Out-File "$out/tools.log" -Append -Encoding utf8
    $messages | Write-Output
    if ($code) { throw "Tool failed: $Program ($code)" }
}

try {
    $bin = $vc.FullName + '/bin/Hostx64/x64'
    $env:PATH = $bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $compiler = $bin + '/cl.exe'
    $rustc = (Get-Command rustc -ErrorAction Stop).Source
    foreach ($entry in @(@('policy','rust/deadline/policy.rs'),
        @('fields','../description/rust/validation/fields.rs'),@('routes','rust/selection/routes.rs'),
        @('recovery','rust/recovery/policy.rs'))) {
        Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-C','panic=abort',
            '-C','opt-level=2','-D','warnings',(Join-Path $module $entry[1]),'-o',"$out/$($entry[0]).obj")
    }
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings',
        (Join-Path $module 'test/rust/deadline/policy_test.rs'),'-o',"$out/rust-tests.exe")
    Invoke-Checked "$out/rust-tests.exe" @()
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings',
        (Join-Path $module 'test/rust/selection/routes_test.rs'),'-o',"$out/route-tests.exe")
    Invoke-Checked "$out/route-tests.exe" @()
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings',
        (Join-Path $module 'test/rust/recovery/policy_test.rs'),'-o',"$out/recovery-tests.exe")
    Invoke-Checked "$out/recovery-tests.exe" @()
    $runner = @'
#include <stdio.h>
int run_watchdog_tests(void);
int run_watchdog_compatibility_tests(void);
int run_watchdog_diagnostics_tests(void);
int run_watchdog_recovery_tests(void);
int main(void) {
    unsigned i;
    for (i = 0; i < 100; ++i) {
        int result = run_watchdog_tests();
        if (result) { printf("WATCHDOG case %d failed\n",result); return 1; }
        result = run_watchdog_compatibility_tests();
        if (result) { printf("COMPATIBILITY case %d failed\n",result); return 1; }
        result = run_watchdog_diagnostics_tests();
        if (result) { printf("DIAGNOSTICS check %d failed\n",result); return 1; }
        result = run_watchdog_recovery_tests();
        if (result) { printf("RECOVERY check %d failed\n",result); return 1; }
    }
    puts("WATCHDOG_ASAN_PASS: 100 repetitions, 10000 successful waits plus fault and rollback paths");
    puts("COMPATIBILITY_ASAN_PASS: 10 scenarios x 100 repetitions; bounds, selection and rollback");
    puts("DIAGNOSTICS_ASAN_PASS: 24 checks x 100 repetitions; first failure, cleanup and recovery");
    puts("RECOVERY_ASAN_PASS: 23 checks x 100 repetitions; policy, quiescence, retry limit and retention");
    return 0;
}
'@
    [IO.File]::WriteAllText("$out/runner.c",$runner,[Text.UTF8Encoding]::new($false))
    $objects = @()
    foreach ($source in @('core.c','device.c','route.c','wait.c','compatibility/select.c','test/platform.c','test/state_test.c',
        'test/compatibility/selection_test.c',
        'test/diagnostics/failure_test.c',
        'recovery/control.c','test/recovery/control_test.c',
        '../../../../../../lib/memory/address_ranges.c')) {
        $object = Join-Path $out ((Split-Path $source -Leaf) + '.obj')
        Invoke-Checked $compiler @('/nologo','/std:c11','/W4','/WX','/TC','/c',
            '/fsanitize=address','/Zi','/MD','/Od',("/I"+$project),"/Fd$out/asan.pdb","/Fo$object",
            (Join-Path $module $source))
        $objects += $object
    }
    $arguments = @('/nologo','/std:c11','/W4','/WX','/fsanitize=address','/Zi','/MD',
        "/Tc$out/runner.c","/Fo$out/runner.obj","/Fd$out/asan.pdb","/Fe$out/tests.exe") +
        $objects + @("$out/fields.obj","$out/policy.obj","$out/routes.obj","$out/recovery.obj",'/link','/incremental:no')
    Invoke-Checked $compiler $arguments
    Copy-Item ($bin + '/clang_rt.asan_dynamic-x86_64.dll') $out
    Invoke-Checked "$out/tests.exe" @()
    $arguments = @('/nologo','/std:c11','/W4','/WX','/fsanitize=address','/Zi','/MD',
        '/Od',("/I"+$project),"/Fd$out/adapter.pdb","/Fo$out/adapter.obj",'/c',
        (Join-Path $module 'hardware/access.c'))
    Invoke-Checked $compiler $arguments
    Invoke-Checked $compiler @('/nologo','/std:c11','/W4','/WX','/fsanitize=address','/Zi','/MD',
        ("/I"+$project),("/Tc"+(Join-Path $module 'test/access/access_test.c')),
        "/Fd$out/adapter.pdb","/Fo$out/adapter-runner.obj","/Fe$out/adapter-tests.exe",
        "$out/adapter.obj",'/link','/incremental:no')
    Invoke-Checked "$out/adapter-tests.exe" @()
    Write-Output "Evidence: $out"
} finally {
    $env:INCLUDE = $savedInclude; $env:LIB = $savedLib; $env:PATH = $savedPath
}
