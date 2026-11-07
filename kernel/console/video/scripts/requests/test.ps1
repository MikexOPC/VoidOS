param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../..").Path
. "$project/scripts/lib/common.ps1"
$out = if ($OutputDirectory) { $OutputDirectory } else {
    "$project/kernel/build/console-requests-" + [guid]::NewGuid().ToString('N')
}
New-Item -ItemType Directory -Force $out | Out-Null
Invoke-Checked "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" @(
    '-B',"$project/test/kernel/console/video/ap/firmware_test.py")
Invoke-Checked "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" @(
    '-B',"$project/test/kernel/scripts/audit_symbols_test.py")

# ============ Rust FFI and fixed ABI ============

$rustc = "$env:USERPROFILE/.cargo/bin/rustc.exe"
$gateway = "$project/kernel/console/video/rust/ffi/requests.rs"
$notify = "$project/test/kernel/console/video/rust/requests/notify_root.rs"
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--test',
    '-D','warnings','--emit=metadata',$notify,'-o',"$out/notify-tests.rmeta")
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--crate-type=lib',
    '-D','warnings','--emit=metadata',$notify,'-o',"$out/notify.rmeta")
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024','--test',
    '-D','warnings','--emit=metadata',"$project/test/kernel/console/video/rust/requests/channel_test.rs",
    '-o',"$out/channel-tests.rmeta")
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024',
    '--crate-type=lib','--emit=metadata','--cfg','console_ap_diagnostic','--cfg','console_ap_notify','-D','warnings',$gateway,'-o',"$out/ffi.rmeta")
foreach ($profile in @('debug','release')) {
    $optimization = if ($profile -eq 'release') { 2 } else { 0 }
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
        '-C','overflow-checks=yes',$notify,'-o',"$out/notify-$profile.exe")
    Invoke-Checked "$out/notify-$profile.exe" @()
    Invoke-Checked $rustc @('--edition=2024','--test','-D','warnings','-C',"opt-level=$optimization",
        '-C','overflow-checks=yes',"$project/test/kernel/console/video/rust/requests/channel_test.rs",
        '-o',"$out/channel-$profile.exe")
    Invoke-Checked "$out/channel-$profile.exe" @()
    Invoke-Checked $rustc @('--edition=2024','--crate-type=staticlib','-D','warnings',
        '--cfg','console_ap_diagnostic','--cfg','console_ap_notify',
        '-C','panic=abort','-C',"opt-level=$optimization",'-C','overflow-checks=yes',
        $gateway,'-o',"$out/requests-$profile.lib")
}
foreach ($source in @('kernel/sync/rust/atomics/operations.rs',
    'kernel/console/video/rust/cursor/plan.rs','kernel/console/video/rust/text/utf8.rs')) {
    $name = [IO.Path]::GetFileNameWithoutExtension($source)
    Invoke-Checked $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2','-C','overflow-checks=yes',"$project/$source",'-o',"$out/$name.obj")
}

# ============ Real kernel locks with host IRQ/context adapters ============

$saved = Use-Msvc
try {
    $sources = @('kernel/console/video/requests/queue.c','kernel/console/video/requests/consumer.c',
        'kernel/sync/lock.c','kernel/sync/acquire.c',
        'kernel/time/clock.c','test/kernel/sync/backend.c',
        'test/kernel/console/video/requests/cases.c','test/kernel/console/video/requests/threads.c',
        'kernel/console/video/service/console.c','kernel/console/video/console.c')
    $objects = @("$out/operations.obj","$out/plan.obj","$out/utf8.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
            '/fsanitize=address','/MD','/O2','/c',"/I$project","$project/$source","/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    foreach ($profile in @('debug','release')) {
        Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/fsanitize=address','/MD',"/Fe$out/host-$profile.exe") +
            $objects + @("$out/requests-$profile.lib",'/link','/incremental:no','/DEBUG','/WX'))
        Invoke-Checked "$out/host-$profile.exe" @()
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
            '/fsanitize=address','/MD','/O2',"/I$project", "/Fo$out/", "/Fd$out/notify.pdb",
            "$project/test/kernel/console/video/requests/notify_ffi.c", "/Fe$out/notify-ffi-$profile.exe",
            '/link',"$out/requests-$profile.lib",'/incremental:no','/DEBUG','/WX')
        foreach ($mode in @('0','1','2')) { Invoke-Checked "$out/notify-ffi-$profile.exe" @($mode) }
    }
    foreach ($case in @('positive','no-ack','inflight')) {
        $defines = if ($case -eq 'no-ack') { @('/DKERNEL_TEST_AP_NOTIFY_NO_ACK=1') }
            elseif ($case -eq 'inflight') { @('/DKERNEL_TEST_AP_NOTIFY_INFLIGHT=1') } else { @() }
        Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
            '/fsanitize=address','/MD','/O2',"/I$project", "/Fo$out/", "/Fd$out/notify-native.pdb",
            "$project/arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.c",
            "$project/arch/x86/kernel/interrupts/apic/icr/icr.c",
            "$project/test/kernel/console/video/requests/notify_native.c", "/Fe$out/notify-native-$case.exe") +
            $defines + @('/link',"$out/requests-release.lib",'/incremental:no','/DEBUG','/WX'))
        foreach ($mode in @('0','1','2','3','4','5')) { Invoke-Checked "$out/notify-native-$case.exe" @($mode) }
    }
    foreach ($profile in @('keyboard','mouse')) {
        $profileFlags = if ($profile -eq 'mouse') { @('/DKERNEL_PS2_MOUSE=1') } else { @() }
        Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
            '/fsanitize=address','/MD','/O2',"/I$project", "/Fo$out/", "/Fd$out/native.pdb",
            "$project/arch/x86/kernel/drivers/video/framebuffer/console/requests.c",
            "$project/test/kernel/console/video/runtime/requests_test.c", "/Fe$out/native-$profile.exe") +
            $profileFlags + @('/link','/incremental:no','/DEBUG','/WX'))
        Invoke-Checked "$out/native-$profile.exe" @()
    }
    Invoke-CppcheckRaw "$out/native-cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" `
        --quiet --platform=win64 --std=c11 --xml --xml-version=2 "-I$project" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/console/requests.c" `
        "$project/test/kernel/console/video/runtime/requests_test.c"
    if ($LASTEXITCODE) { throw 'Native adapter cppcheck failed' }
    [xml]$nativeReport = Get-Content "$out/native-cppcheck.xml"
    if (@($nativeReport.results.errors.error | Where-Object { $_.severity -in 'error','warning','performance','portability' }).Count) {
        throw 'Blocking native adapter diagnostics'
    }
    Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" `
        --quiet --platform=win64 --std=c11 --xml --xml-version=2 "-I$project" `
        "$project/kernel/console/video/requests/queue.c" `
        "$project/test/kernel/console/video/requests/cases.c" `
        "$project/test/kernel/console/video/requests/threads.c"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$report = Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object { $_.severity -in 'error','warning','performance','portability' }).Count) {
        throw 'Blocking requests static diagnostics'
    }
    Invoke-CppcheckRaw "$out/ap-cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" `
        --quiet --platform=win64 --std=c11 --xml --xml-version=2 "-I$project" `
        -DKERNEL_TEST_AP_SERVICE=9 -DKERNEL_TEST_AP_CONSOLE=1 `
        "$project/test/kernel/console/video/ap/channel.c" "$project/test/arch/x86/kernel/scheduler/batch/probe.c" `
        "$project/arch/x86/kernel/scheduler/batch/work.c" "$project/arch/x86/kernel/scheduler/batch/runtime.c"
    if ($LASTEXITCODE) { throw 'AP diagnostic cppcheck failed' }
    [xml]$apReport = Get-Content "$out/ap-cppcheck.xml"
    if (@($apReport.results.errors.error | Where-Object { $_.severity -in 'error','warning','performance','portability' }).Count) {
        throw 'Blocking AP diagnostic diagnostics'
    }
    foreach ($case in @('positive','no-ack','inflight')) {
        $directory = "$out/notify-static-$case"
        New-Item -ItemType Directory -Force $directory | Out-Null
        $definitions = @('KERNEL_TEST_AP_SERVICE=9','KERNEL_TEST_AP_CONSOLE=1','KERNEL_TEST_AP_NOTIFY=1')
        if ($case -eq 'no-ack') { $definitions += 'KERNEL_TEST_AP_NOTIFY_NO_ACK=1' }
        if ($case -eq 'inflight') { $definitions += 'KERNEL_TEST_AP_NOTIFY_INFLIGHT=1' }
        Invoke-Cppcheck $project $directory @(
            "$project/arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.c",
            "$project/arch/x86/kernel/drivers/video/framebuffer/console/wake/hardware.c",
            "$project/test/kernel/console/video/ap/channel.c",
            "$project/test/kernel/console/video/requests/notify_ffi.c",
            "$project/test/kernel/console/video/requests/notify_native.c") $definitions
    }
} finally { Restore-Msvc $saved }
"PASS: console request gateway evidence $out"
