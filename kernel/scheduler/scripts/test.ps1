param([string]$CppcheckPath)
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../..')).Path
$out = Join-Path $project ('kernel/build/scheduler-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory |
    Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE; $savedLib = $env:LIB; $savedPath = $env:PATH
$savedClippyConfig = $env:CLIPPY_CONF_DIR

# ============ checked tool execution ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    "Program: $Program" | Out-File "$out/tools.log" -Append -Encoding utf8
    ('Arguments: ' + ($Arguments -join ' ')) | Out-File "$out/tools.log" -Append -Encoding utf8
    $messages | Out-File "$out/tools.log" -Append -Encoding utf8
    $messages | Write-Output
    if ($code) { throw "Tool failed: $Program ($code)" }
}

# ============ module host validation ============

try {
    if (!$vc -or !$version) { throw 'MSVC/Windows SDK unavailable' }
    $bin = $vc.FullName + '/bin/Hostx64/x64'
    $env:PATH = $bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $rustc = (Get-Command rustc -ErrorAction Stop).Source
    $clippy = (Get-Command clippy-driver -ErrorAction Stop).Source
    $env:CLIPPY_CONF_DIR = $PSScriptRoot
    Invoke-Checked $rustc @('--version')
    foreach ($mode in @('debug','release')) {
        $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
            (Join-Path $module 'rust/state/owner.rs'),'-o',"$out/state-$mode.exe")
        if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
        Invoke-Checked $rustc $flags
        Invoke-Checked "$out/state-$mode.exe" @('--nocapture')
        $bindingFlags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
            (Join-Path $module 'rust/irq/bindings.rs'),'-o',"$out/bindings-$mode.exe")
        if ($mode -eq 'release') { $bindingFlags += @('-C','opt-level=2') }
        Invoke-Checked $rustc $bindingFlags
        Invoke-Checked "$out/bindings-$mode.exe" @('--nocapture')
        $payloadFlags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
            (Join-Path $module 'rust/payload/channel.rs'),'-o',"$out/payload-$mode.exe")
        if ($mode -eq 'release') { $payloadFlags += @('-C','opt-level=2') }
        Invoke-Checked $rustc $payloadFlags
        Invoke-Checked "$out/payload-$mode.exe" @('--nocapture')
    }
    Invoke-Checked $clippy @('--edition=2024','--test','-D','warnings','--emit=metadata',
        (Join-Path $module 'rust/state/owner.rs'),'-o',"$out/core-clippy.rmeta")
    Invoke-Checked $clippy @('--edition=2024','--crate-type=lib','-C','panic=abort',
        '-D','warnings','--emit=metadata',(Join-Path $module 'rust/ffi/gateway.rs'),
        '-o',"$out/gateway-clippy.rmeta")
    Invoke-Checked $clippy @('--edition=2024','--test','-D','warnings','--emit=metadata',
        (Join-Path $module 'rust/payload/channel.rs'),'-o',"$out/payload-clippy.rmeta")
    Invoke-Checked $rustc @('--edition=2024','--crate-type=staticlib','--target','x86_64-pc-windows-msvc',
        '--cfg','scheduler_host_unbound',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes','-D','warnings',
        (Join-Path $module 'rust/ffi/gateway.rs'),'-o',"$out/scheduler.lib")
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/DSCHEDULER_HOST_UNBOUND',
        '/MD','/O2',('/I' + $project),(Join-Path $module '../../test/kernel/scheduler/ffi/gateway_test.c'),
        ("/Fo$out/gateway-test.obj"),("/Fd$out/gateway-test.pdb"),("/Fe$out/gateway-test.exe"),
        '/link','/incremental:no',"$out/scheduler.lib")
    Invoke-Checked "$out/gateway-test.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/DSCHEDULER_HOST_UNBOUND',
        '/MD','/O2',('/I' + $project),(Join-Path $project 'arch/x86/kernel/scheduler/dispatcher/engine.c'),
        (Join-Path $project 'test/arch/x86/kernel/scheduler/dispatcher/engine_test.c'),
        ("/Fo$out/"),("/Fd$out/engine.pdb"),("/Fe$out/engine.exe"),
        '/link','/incremental:no',"$out/scheduler.lib")
    foreach ($mode in 0..8) { Invoke-Checked "$out/engine.exe" @("$mode") }
    # Production gateway: event-only posting is not exported in this library.
    Invoke-Checked $rustc @('--edition=2024','--crate-type=staticlib','--target','x86_64-pc-windows-msvc',
        '-C','panic=abort','-C','opt-level=2','-D','warnings',
        (Join-Path $module 'rust/ffi/gateway.rs'),'-o',"$out/scheduler-bound.lib")
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',('/I' + $project),(Join-Path $module '../../test/kernel/scheduler/ffi/binding_test.c'),
        ("/Fo$out/binding-test.obj"),("/Fd$out/binding-test.pdb"),("/Fe$out/binding-test.exe"),
        '/link','/incremental:no',"$out/scheduler-bound.lib")
    Invoke-Checked "$out/binding-test.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',('/I' + $project),(Join-Path $module '../../test/kernel/scheduler/ffi/payload_test.c'),
        ("/Fo$out/payload-test.obj"),("/Fd$out/payload-test.pdb"),("/Fe$out/payload-test.exe"),
        '/link','/incremental:no',"$out/scheduler-bound.lib")
    Invoke-Checked "$out/payload-test.exe" @()
    $bindingSymbols = & "$bin/dumpbin.exe" /symbols "$out/scheduler-bound.lib" 2>&1
    if ($LASTEXITCODE) { throw 'Production symbol inspection failed' }
    if ($bindingSymbols -match '\| kernel_scheduler_irq_post\s*$') {
        throw 'Unbound IRQ post exported in production gateway'
    }
    'PASS: production gateway has no event-only IRQ post symbol' | Write-Output
    if (!$CppcheckPath) {
        $tool = Get-Command cppcheck -ErrorAction SilentlyContinue
        $CppcheckPath = if ($tool) { $tool.Source } else {
            Join-Path $project 'kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe'
        }
    }
    if (!(Test-Path -LiteralPath $CppcheckPath)) { throw 'Supply -CppcheckPath for C fixture analysis' }
    $cppcheckArguments = @('--enable=all','--check-level=exhaustive','--platform=win64',
        '--std=c11','--xml','--xml-version=2','-DSCHEDULER_HOST_UNBOUND',('-I' + $project),
        (Join-Path $module '../../test/kernel/scheduler/ffi/gateway_test.c'),(Join-Path $module '../../test/kernel/scheduler/ffi/binding_test.c'),
        (Join-Path $module '../../test/kernel/scheduler/ffi/payload_test.c'))
    & $CppcheckPath @cppcheckArguments 2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck execution failed' }
    [xml]$analysis = Get-Content -LiteralPath "$out/cppcheck.xml"
    $blocking = @($analysis.results.errors.error | Where-Object { $_.severity -in @('error','warning','performance','portability') })
    if ($blocking.Count) { throw 'Blocking cppcheck diagnostics' }
    Write-Output 'Cppcheck: zero error/warning/performance/portability; inspect style/information in cppcheck.xml'
    $sources = @(Get-ChildItem $module -Recurse -File | Where-Object { $_.Extension -in @('.rs','.c','.h','.ps1','.toml') })
    $sources += @(Get-Item (Join-Path $project 'arch/x86/kernel/scheduler/dispatcher/engine.c'),
        (Join-Path $project 'arch/x86/kernel/scheduler/dispatcher/engine.h'),
        (Join-Path $project 'test/arch/x86/kernel/scheduler/dispatcher/engine_test.c'))
    $sources | ForEach-Object { Get-FileHash -LiteralPath $_.FullName } |
        ConvertTo-Json | Out-File "$out/sources.json" -Encoding utf8
    Write-Output "PASS: scheduler host evidence $out"
} finally {
    $env:INCLUDE = $savedInclude; $env:LIB = $savedLib; $env:PATH = $savedPath
    $env:CLIPPY_CONF_DIR = $savedClippyConfig
}
