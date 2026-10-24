param([string]$OutputDirectory, [string]$Python)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../..").Path
$out = if ($OutputDirectory) { $OutputDirectory } else { "$project/kernel/build/console-grid-host" }
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../scripts/lib/common.ps1')

# ============ firmware harness regression tests ============

if (!$Python) {
    $Python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
}
foreach ($name in @('firmware_test.py', 'active_test.py', 'shell_pixels_test.py')) {
    Invoke-Checked $Python @('-B', "$project/test/kernel/console/video/scripts/$name")
}

# ============ supported host ABI ============

$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH
try {
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $rustc="$env:USERPROFILE/.cargo/bin/rustc.exe"
    foreach ($profile in @('debug','release')) {
        $optimization = if ($profile -eq 'release') { '2' } else { '0' }
        Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
            '-C','overflow-checks=yes',"$project/test/kernel/console/video/rust/text/utf8_test.rs",
            '-o',"$out/text-$profile.exe")
        Invoke-Checked "$out/text-$profile.exe" @()
    }
    Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
        '--emit=metadata','-D','warnings',"$project/kernel/console/video/rust/text/utf8.rs",'-o',"$out/text-clippy.rmeta")
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',
        "$project/kernel/console/video/rust/text/utf8.rs",'-o',"$out/text.obj")
    foreach ($profile in @('debug','release')) {
        $optimization = if ($profile -eq 'release') { '2' } else { '0' }
        Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
            '-C','overflow-checks=yes',"$project/test/kernel/console/video/rust/cursor/plan_test.rs",
            '-o',"$out/$profile.exe")
        Invoke-Checked "$out/$profile.exe" @()
        Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
            '-C','overflow-checks=yes',"$project/test/kernel/console/video/rust/timing/budget_test.rs",
            '-o',"$out/timing-$profile.exe")
        Invoke-Checked "$out/timing-$profile.exe" @()
    }
    Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
        '--emit=metadata','-D','warnings',"$project/kernel/console/video/rust/cursor/plan.rs",'-o',"$out/clippy.rmeta")
    Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
        '--emit=metadata','-D','warnings',"$project/kernel/console/video/rust/ffi/timing.rs",'-o',"$out/timing-clippy.rmeta")
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',
        "$project/kernel/console/video/rust/ffi/timing.rs",'-o',"$out/timing.obj")
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',
        "$project/kernel/console/video/rust/cursor/plan.rs",'-o',"$out/plan.obj")
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/kernel/console/video/console.c","$project/kernel/console/video/render.c",
        "$project/kernel/console/video/measurement/budget.c",
        "$project/test/kernel/console/video/measurement/measurement_cases.c",
        "$project/test/kernel/console/video/host/cases.c","$project/test/kernel/console/video/host/render_cases.c",
        ("/Fo$out/"),("/Fd$out/host.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/kernel/console/video/batch/render.c",("/Fo$out/batch-render.obj"),("/Fd$out/host.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/fsanitize=address','/MD',
        "$out/console.obj","$out/render.obj","$out/cases.obj","$out/render_cases.obj","$out/plan.obj",
        "$out/budget.obj","$out/batch-render.obj","$out/measurement_cases.obj","$out/timing.obj",("/Fe$out/host.exe"),
        '/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/host.exe" @()
    . "$PSScriptRoot/pointer/test.ps1"
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/kernel/console/video/service/console.c",("/Fo$out/service.obj"),("/Fd$out/service.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/test/kernel/console/video/service/cases.c",("/Fo$out/service-cases.obj"),("/Fd$out/service.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/fsanitize=address','/MD',
        "$out/console.obj","$out/plan.obj","$out/text.obj","$out/service.obj","$out/service-cases.obj",
        ("/Fe$out/service.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/service.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
        "$project/test/kernel/console/video/batch/cases.c",("/Fo$out/batch-cases.obj"),("/Fd$out/host.pdb"))
    Invoke-Checked "$bin/cl.exe" @('/nologo','/fsanitize=address','/MD',
        "$out/console.obj","$out/render.obj","$out/plan.obj","$out/budget.obj","$out/timing.obj",
        "$out/batch-render.obj","$out/batch-cases.obj",("/Fe$out/batch.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/batch.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        "$project/test/kernel/console/video/profiling/profile.c",
        "$project/test/kernel/console/video/profiling/cases.c",
        ("/Fo$out/"),("/Fd$out/profile.pdb"),("/Fe$out/profile.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/profile.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        ("/I$project"),"$project/test/kernel/console/video/active/snapshot.c",
        "$project/test/kernel/console/video/active/snapshot_cases.c",
        ("/Fo$out/"),("/Fd$out/snapshot.pdb"),("/Fe$out/snapshot.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/snapshot.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        ("/I$project"),"$project/arch/x86/kernel/scheduler/stacks.c",
        "$project/arch/x86/kernel/scheduler/capabilities.c",
        "$project/arch/x86/kernel/scheduler/mapping/native.c",
        "$project/test/arch/x86/kernel/scheduler/mapping/native_test.c",
        ("/Fo$out/"),("/Fd$out/native.pdb"),("/Fe$out/native.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/native.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        ("/I$project"),"$project/arch/x86/kernel/drivers/video/framebuffer/live/group.c",
        "$project/test/arch/x86/kernel/drivers/video/framebuffer/live/cases.c",
        ("/Fo$out/"),("/Fd$out/live.pdb"),("/Fe$out/live.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/live.exe" @()
    foreach ($profile in @('keyboard','mouse')) {
        $profileFlags = @('/DKERNEL_CONSOLE_SHELL=1')
        if ($profile -eq 'mouse') { $profileFlags += '/DKERNEL_PS2_MOUSE=1' }
        foreach ($case in @('worker','session')) {
            $target = "$out/$case-$profile"
            Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/Gy',
                ("/I$project"),"$project/test/kernel/console/video/runtime/${case}_test.c",
                ("/Fo$target.obj"),("/Fd$target.pdb"),("/Fe$target.exe")) + $profileFlags +
                @('/link','/incremental:no','/DEBUG','/OPT:REF','/WX'))
            Invoke-Checked "$target.exe" @()
        }
    }
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',
        "$project/arch/x86/kernel/drivers/video/framebuffer/rust/drawing/validation.rs",'-o',"$out/drawing.obj")
    $gatewayObjects = @("$out/drawing.obj")
    foreach ($source in @('gateway/cell.c','../../../../../../test/arch/x86/kernel/drivers/video/framebuffer/gateway/cases.c','font/font.c')) {
        $object = "$out/gateway-" + $source.Replace('/','-').Replace('.c','.obj')
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2','/c',
            ("/I$project"),"$project/arch/x86/kernel/drivers/video/framebuffer/$source",("/Fo$object"),("/Fd$out/host.pdb"))
        $gatewayObjects += $object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/gateway.exe")) +
        $gatewayObjects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/gateway.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        ("/I$project"),"$project/arch/x86/kernel/drivers/video/framebuffer/gateway/inventory.c",
        "$project/test/arch/x86/kernel/drivers/video/framebuffer/gateway/inventory_cases.c",
        ("/Fo$out/"),("/Fd$out/host.pdb"),("/Fe$out/inventory.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/inventory.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',
        '/DKERNEL_TEST_CONSOLE_ACTIVE=1',("/I$project"),
        "$project/test/kernel/console/video/host/hpet_policy.c",
        "$project/arch/x86/kernel/timer/hpet/description/probe/policy.c",
        ("/Fo$out/"),("/Fd$out/host.pdb"),("/Fe$out/hpet-policy.exe"),'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/hpet-policy.exe" @()
    $cppcheck = "$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") "$project/kernel/console/video/console.c" `
        "$project/kernel/console/video/pointer/state.c" "$project/kernel/console/video/pointer/shape.c" `
        "$project/test/kernel/console/video/pointer/state_test.c" `
        "$project/kernel/console/video/service/console.c" "$project/test/kernel/console/video/service/cases.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/live/group.c" `
        "$project/test/arch/x86/kernel/drivers/video/framebuffer/live/cases.c" `
        "$project/kernel/console/video/render.c" "$project/test/kernel/console/video/host/render_cases.c" `
        "$project/kernel/console/video/measurement/budget.c" `
        "$project/test/kernel/console/video/measurement/measurement_cases.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/gateway/cell.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/gateway/native.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/gateway/inventory.c" `
        "$project/test/arch/x86/kernel/drivers/video/framebuffer/gateway/inventory_cases.c" `
        "$project/test/arch/x86/kernel/drivers/video/framebuffer/gateway/cases.c" `
        "$project/test/kernel/console/video/kernel/render_probe.c" `
        "$project/test/kernel/console/video/host/cases.c" "$project/test/kernel/console/video/kernel/probe.c" `
        2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml = Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        -DKERNEL_TEST_CONSOLE_ACTIVE=1 -DKERNEL_TEST_CONSOLE_DRAW=1 -DKERNEL_TEST_CONSOLE_BATCH=1 -DKERNEL_PS2_INPUT=1 -DKERNEL_SCHEDULER_IRQ=1 ("-I$project") `
        "$project/test/kernel/console/video/active/worker.c" `
        "$project/test/kernel/console/video/profiling/profile.c" `
        "$project/test/kernel/console/video/profiling/cases.c" `
        "$project/kernel/console/video/batch/render.c" `
        "$project/test/kernel/console/video/batch/cases.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/drawing/draw.c" `
        "$project/test/kernel/console/video/active/snapshot.c" `
        "$project/test/kernel/console/video/active/snapshot_cases.c" `
        "$project/arch/x86/kernel/scheduler/mapping/native.c" `
        "$project/test/arch/x86/kernel/scheduler/mapping/native_test.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/integration/kernel.c" `
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/task.c" `
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/session.c" `
        "$project/test/kernel/console/video/host/hpet_policy.c" `
        "$project/arch/x86/kernel/timer/hpet/description/probe/policy.c" `
        "$project/arch/x86/kernel/timer/hpet/watchdog/core.c" `
        "$project/arch/x86/kernel/timer/hpet/watchdog/recovery/control.c" `
        "$project/arch/x86/kernel/timer/lapic/driver.c" 2> "$out/cppcheck-active.xml"
    if ($LASTEXITCODE) { throw 'Active cppcheck failed' }
    [xml]$activeXml = Get-Content "$out/cppcheck-active.xml"
    if (@($activeXml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking active static diagnostics'
    }
    foreach ($profile in @('keyboard','mouse')) {
        $profileFlags = @()
        if ($profile -eq 'mouse') { $profileFlags += '-DKERNEL_PS2_MOUSE=1' }
        & $cppcheck @profileFlags --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
            -DKERNEL_CONSOLE_SHELL=1 -DKERNEL_PS2_INPUT=1 -DKERNEL_SCHEDULER_IRQ=1 ("-I$project") `
            "$project/kernel/console/video/service/console.c" "$project/kernel/shell/shell.c" `
            "$project/arch/x86/kernel/drivers/video/framebuffer/live/group.c" `
            "$project/arch/x86/kernel/drivers/video/framebuffer/console/runtime.c" `
            "$project/arch/x86/kernel/drivers/video/framebuffer/integration/kernel.c" `
            "$project/arch/x86/kernel/drivers/input/ps2/runtime/task.c" `
            "$project/arch/x86/kernel/drivers/input/ps2/runtime/session.c" `
            "$project/arch/x86/kernel/drivers/input/ps2/shell/task.c" `
            "$project/arch/x86/kernel/scheduler/integration/kernel.c" `
            "$project/arch/x86/kernel/scheduler/mapping/native.c" 2> "$out/cppcheck-shell-$profile.xml"
        if ($LASTEXITCODE) { throw 'Operational console cppcheck failed' }
        [xml]$shellXml = Get-Content "$out/cppcheck-shell-$profile.xml"
        if (@($shellXml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
            throw 'Blocking operational console static diagnostics'
        }
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: console grid host evidence $out"
