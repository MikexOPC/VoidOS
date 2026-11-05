$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../..')).Path
$out = Join-Path $project ('kernel/build/scheduler-context-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory |
    Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE; $savedLib = $env:LIB; $savedPath = $env:PATH

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../scripts/lib/common.ps1')
function Invoke-Checked([string]$Program, [string[]]$Arguments) { Invoke-OsChecked $Program $Arguments "$out/tools.log" -Tee }

# ============ bounded owned test process ============

function Invoke-Test([string]$Name, [string]$Argument = '') {
    $process = Start-Process "$out/$Name.exe" -WindowStyle Hidden -PassThru -WorkingDirectory $out `
        -RedirectStandardOutput "$out/$Name.stdout" -RedirectStandardError "$out/$Name.stderr" -ArgumentList $(if ($Argument) {$Argument} else {' '})
    if (!$process.WaitForExit(30000)) {
        $process.Kill(); $process.WaitForExit(); throw "Owned test timeout: $Name"
    }
    $process.Refresh()
    Get-Content "$out/$Name.stdout"
    Get-Content "$out/$Name.stderr"
    if ($process.ExitCode -ne 0) { throw "Test failed: $Name ($($process.ExitCode))" }
}

# ============ backend validation ============

try {
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $bin = $vc.FullName + '/bin/Hostx64/x64'
    $env:PATH = $bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' +
        $sdk + '/Lib/' + $version.Name + '/um/x64'
    $asm = @('assembly/switch/context.asm','assembly/profile/capture.asm',
        '../../../../test/arch/x86/kernel/scheduler/assembly/preservation/probe.asm','assembly/profile/permissions.asm','assembly/irq/compute.asm')
    $objects = @()
    foreach ($index in 0..($asm.Count - 1)) {
        $object = "$out/asm-$index.obj"
        Invoke-Checked "$bin/ml64.exe" @('/nologo','/c','/W3','/WX',("/Fo$object"),(Join-Path $module $asm[$index]))
        $objects += $object
        & "$bin/dumpbin.exe" /disasm $object | Out-File "$out/asm-$index.disasm" -Encoding utf8
        if ($LASTEXITCODE) { throw 'Disassembly failed' }
    }
    foreach ($name in @('state','switch','cost')) {
        $flags = @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',('/I' + $project),
            (Join-Path $module 'context.c'),(Join-Path $module 'capabilities.c'),
            (Join-Path $project "test/arch/x86/kernel/scheduler/context/${name}_test.c"),("/Fo$out/"),("/Fd$out/$name.pdb"),("/Fe$out/$name.exe"))
        if ($name -eq 'state') { $flags += '/fsanitize=address' }
        $flags += @('/link','/incremental:no','/CETCOMPAT:NO') + $objects
        Invoke-Checked "$bin/cl.exe" $flags
        foreach ($object in @('context','capabilities',"${name}_test")) {
            & "$bin/dumpbin.exe" /disasm "$out/$object.obj" |
                Out-File "$out/$name-$object.disasm" -Encoding utf8
            if ($LASTEXITCODE) { throw 'C disassembly failed' }
        }
        foreach ($repetition in 1..10) {
            Invoke-Test $name
            Copy-Item "$out/$name.stdout" "$out/$name-$repetition.stdout"
            Copy-Item "$out/$name.stderr" "$out/$name-$repetition.stderr"
        }
    }
    $pattern = '^\s*[0-9A-F]+:\s+(?:[0-9A-F]{2}\s+)+\s*(?:v[a-z0-9]+|tile[a-z0-9]+)\s'
    $extended = Get-ChildItem $out -Filter '*.disasm' | Select-String -Pattern $pattern
    if ($extended) { throw 'Instruction outside SSE/x87 profile; inspect disassembly' }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),(Join-Path $project 'test/kernel/mm/physical/synchronization/reader_model.c'),(Join-Path $module 'stacks.c'),
        (Join-Path $module '../../../../test/arch/x86/kernel/scheduler/stacks/resources_test.c'),("/Fo$out/"),("/Fd$out/resources.pdb"),
        ("/Fe$out/resources.exe"),'/link','/incremental:no')
    foreach ($object in @('stacks','resources_test')) {
        & "$bin/dumpbin.exe" /disasm "$out/$object.obj" |
            Out-File "$out/resources-$object.disasm" -Encoding utf8
        if ($LASTEXITCODE) { throw 'Resource disassembly failed' }
    }
    if (Get-ChildItem $out -Filter 'resources-*.disasm' | Select-String -Pattern $pattern) {
        throw 'Resource instruction outside SSE/x87 profile'
    }
    foreach ($repetition in 1..10) {
        Invoke-Test 'resources'
        Copy-Item "$out/resources.stdout" "$out/resources-$repetition.stdout"
        Copy-Item "$out/resources.stderr" "$out/resources-$repetition.stderr"
    }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),(Join-Path $project 'test/kernel/mm/physical/synchronization/reader_model.c'),(Join-Path $module 'stacks.c'),
        (Join-Path $module 'capabilities.c'),(Join-Path $module 'mapping/native.c'),
        (Join-Path $module '../../../../test/arch/x86/kernel/scheduler/mapping/native_test.c'),("/Fo$out/"),("/Fd$out/native.pdb"),
        ("/Fe$out/native.exe"),'/link','/incremental:no')
    foreach ($repetition in 1..10) {
        Invoke-Test 'native'
        Copy-Item "$out/native.stdout" "$out/native-$repetition.stdout"
        Copy-Item "$out/native.stderr" "$out/native-$repetition.stderr"
    }
    $cppcheck = Join-Path $project 'kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe'
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),(Join-Path $module 'irq/bridge.c'),
        (Join-Path $module '../../../../test/arch/x86/kernel/scheduler/irq/bridge_test.c'),("/Fo$out/"),("/Fd$out/irq.pdb"),
        ("/Fe$out/irq.exe"),'/link','/incremental:no',$objects[4])
    foreach ($repetition in 1..10) { Invoke-Test 'irq' }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),(Join-Path $module 'idle/idle.c'),
        (Join-Path $module '../../../../test/arch/x86/kernel/scheduler/idle/idle_test.c'),("/Fo$out/"),("/Fd$out/idle.pdb"),
        ("/Fe$out/idle.exe"),'/link','/incremental:no')
    foreach ($repetition in 1..10) { foreach ($mode in 0..9) { Invoke-Test 'idle' "$mode" } }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),
        (Join-Path $project 'arch/x86/kernel/timer/hpet/watchdog/accounting/budget.c'),
        (Join-Path $project 'test/kernel/mm/physical/synchronization/reader_model.c'),
        (Join-Path $project 'lib/memory/address_ranges.c'),(Join-Path $module '../../../../test/arch/x86/kernel/scheduler/idle/budget_test.c'),
        ("/Fo$out/"),("/Fd$out/budget.pdb"),("/Fe$out/budget.exe"),'/link','/incremental:no')
    foreach ($repetition in 1..10) { Invoke-Test 'budget' }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/MD','/O2',
        '/fsanitize=address',('/I' + $project),(Join-Path $module 'integration/lock.c'),
        (Join-Path $project 'test/arch/x86/kernel/scheduler/integration/authority_test.c'),
        ("/Fo$out/"),("/Fd$out/authority.pdb"),("/Fe$out/authority.exe"),'/link','/incremental:no')
    Invoke-Test 'authority'
    if (!(Test-Path $cppcheck)) { throw 'Cppcheck unavailable' }
    $inputs = @('context.c','capabilities.c','stacks.c','../../../../test/arch/x86/kernel/scheduler/stacks/resources_test.c',
        'mapping/native.c','mapping/access.c','integration/kernel.c','integration/workload.c','irq/bridge.c','runtime/memory.c','runtime/payload.c',
        '../../../../test/arch/x86/kernel/scheduler/mapping/native_test.c','../../../../test/arch/x86/kernel/scheduler/context/state_test.c','../../../../test/arch/x86/kernel/scheduler/context/switch_test.c','../../../../test/arch/x86/kernel/scheduler/irq/bridge_test.c',
        'idle/idle.c','integration/report.c','persistent/workload.c','../../../../test/arch/x86/kernel/scheduler/fault/fault.c',
        'dispatcher/engine.c','diagnostic/profiles.c','../../../../test/arch/x86/kernel/scheduler/dispatcher/engine_test.c',
        'integration/lock.c','../../../../test/arch/x86/kernel/scheduler/integration/authority_test.c',
        '../../../../test/arch/x86/kernel/scheduler/idle/idle_test.c','../../../../test/arch/x86/kernel/scheduler/idle/budget_test.c') |
        ForEach-Object { Join-Path $module $_ }
    Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" "--quiet" --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") @inputs
    if ($LASTEXITCODE) { throw 'Cppcheck execution failed' }
    [xml]$analysis = Get-Content "$out/cppcheck.xml"
    $blocking = @($analysis.results.errors.error | Where-Object {
        $_.severity -in @('error','warning','performance','portability')
    })
    if ($blocking.Count) { throw 'Blocking cppcheck diagnostics' }
    Get-ChildItem $module -Recurse -File | Where-Object { $_.Extension -in @('.c','.h','.asm','.ps1') } |
        ForEach-Object { Get-FileHash -LiteralPath $_.FullName } |
        ConvertTo-Json | Out-File "$out/sources.json" -Encoding utf8
    Write-Output "PASS: backend host evidence $out"
} finally {
    $env:INCLUDE = $savedInclude; $env:LIB = $savedLib; $env:PATH = $savedPath
}
