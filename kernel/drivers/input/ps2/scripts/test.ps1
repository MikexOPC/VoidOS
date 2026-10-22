param([string]$Python)
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $module '../../../..')).Path
$out = Join-Path $project ('kernel/build/ps2-decoder-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$source = Join-Path $project 'kernel/drivers/input/ps2/rust/decoder/set2.rs'

# ============ checked Rust tool execution ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    $messages | Out-File "$out/tools.log" -Append -Encoding utf8
    $messages | Write-Output
    if ($code) { throw "Tool failed: $Program ($code)" }
}

# ============ decoder validation ============

if (!$Python) {
    $Python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
}
Invoke-Checked $Python @("$PSScriptRoot/structure.py")
& "$PSScriptRoot/mouse/test.ps1" -OutputDirectory "$out/mouse"
Invoke-Checked 'rustc' @('--version')
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        $source,'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/$mode.exe" @('--nocapture')
}
Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',$source,'-o',"$out/set2.rlib")
foreach ($textModule in @('italian','utf8')) {
    foreach ($mode in @('debug','release')) {
        $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
            "$project/kernel/input/keyboard/rust/text/$textModule.rs",'-o',"$out/$textModule-$mode.exe")
        if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
        Invoke-Checked 'rustc' $flags
        Invoke-Checked "$out/$textModule-$mode.exe" @()
    }
    Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings','--emit=metadata',
        "$project/kernel/input/keyboard/rust/text/$textModule.rs",'-o',"$out/$textModule-clippy.rmeta")
}
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        "$project/kernel/console/line/rust/editor/plan.rs",'-o',"$out/line-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/line-$mode.exe" @()
}
Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings','--emit=metadata',
    "$project/kernel/console/line/rust/editor/plan.rs",'-o',"$out/line-clippy.rmeta")
foreach ($shellModule in @('command','queue')) {
    foreach ($mode in @('debug','release')) {
        $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
            "$project/kernel/shell/rust/command/$shellModule.rs",'-o',"$out/shell-$shellModule-$mode.exe")
        if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
        Invoke-Checked 'rustc' $flags
        Invoke-Checked "$out/shell-$shellModule-$mode.exe" @()
    }
    Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings','--emit=metadata',
        "$project/kernel/shell/rust/command/$shellModule.rs",'-o',"$out/shell-$shellModule-clippy.rmeta")
}
Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings',
    '--emit=metadata',$source,'-o',"$out/clippy.rmeta")
$gateway = Join-Path $project 'kernel/input/integration/rust/ffi/ps2.rs'
Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',$gateway,'-o',"$out/decoder.obj")
Invoke-Checked 'clippy-driver' @('--edition=2024','--crate-type=lib','-D','warnings',
    '--emit=metadata',$gateway,'-o',"$out/gateway.rmeta")
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
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/DKERNEL_PS2_HOST_TEST','/MD','/O2',("/I$project"),"$project/test/kernel/drivers/input/ps2/decoder/abi_test.c",
        ("/Fo$out/abi.obj"),("/Fd$out/abi.pdb"),("/Fe$out/abi.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/abi.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$project/test/kernel/input/keyboard/text/abi_test.c",
        ("/Fo$out/text.obj"),("/Fd$out/text.pdb"),("/Fe$out/text.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/text.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$project/test/kernel/console/line/editor/abi_test.c","$project/kernel/console/line/line.c",
        ("/Fo$out/"),("/Fd$out/line.pdb"),("/Fe$out/line.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/line.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$project/test/kernel/shell/command/abi_test.c","$project/kernel/shell/shell.c",
        "$project/kernel/shell/queue.c","$project/kernel/console/line/line.c",("/Fo$out/"),("/Fd$out/shell.pdb"),
        ("/Fe$out/shell.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/shell.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$project/test/arch/x86/kernel/drivers/input/ps2/shell/bridge_test.c",
        "$project/arch/x86/kernel/drivers/input/ps2/shell/task.c","$project/kernel/shell/queue.c","$project/kernel/console/line/line.c",
        ("/Fo$out/"),("/Fd$out/bridge.pdb"),("/Fe$out/bridge.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/bridge.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/DKERNEL_CONSOLE_SHELL=1',("/I$project"),
        "$project/test/arch/x86/kernel/drivers/input/ps2/shell/bridge_test.c",
        "$project/arch/x86/kernel/drivers/input/ps2/shell/task.c","$project/kernel/shell/queue.c","$project/kernel/console/line/line.c",
        ("/Fo$out/"),("/Fd$out/console-bridge.pdb"),("/Fe$out/console-bridge.exe"),'/link','/incremental:no',"$out/decoder.obj")
    Invoke-Checked "$out/console-bridge.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',("/I$project"),"$project/kernel/drivers/input/ps2/controller/controller.c","$project/kernel/drivers/input/ps2/controller/transport.c",
        "$project/test/kernel/drivers/input/ps2/controller/transport_test.c",("/Fo$out/"),("/Fd$out/controller.pdb"),
        ("/Fe$out/controller.exe"),'/link','/incremental:no')
    Invoke-Checked "$out/controller.exe" @()
    foreach ($dependency in @('kernel/firmware/acpi/rust/root/layout.rs','lib/firmware/acpi/rust/rsdp/validation.rs')) {
        $name = ($dependency -replace '/','-' -replace '\.rs$','.obj')
        Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
            '-C','panic=abort','-C','opt-level=2',"$project/$dependency",'-o',"$out/$name")
    }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/Gy',("/I$project"),"$project/kernel/drivers/input/ps2/controller/discovery.c","$project/test/kernel/drivers/input/ps2/controller/discovery_test.c",
        "$project/test/kernel/drivers/input/ps2/controller/platform.c",
        "$project/kernel/firmware/acpi/root.c","$project/lib/firmware/acpi/rsdp.c", "$project/lib/memory/address_ranges.c",
        ("/Fo$out/"),("/Fd$out/discovery.pdb"),("/Fe$out/discovery.exe"),'/link','/incremental:no','/opt:ref',
        "$out/kernel-firmware-acpi-rust-root-layout.obj","$out/lib-firmware-acpi-rust-rsdp-validation.obj")
    Invoke-Checked "$out/discovery.exe" @()
    foreach ($dependency in @('kernel/firmware/acpi/madt/rust/records/validation.rs',
        'arch/x86/kernel/interrupts/apic/rust/registers/validation.rs')) {
        $name = ($dependency -replace '/','-' -replace '\.rs$','.obj')
        Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
            '-C','panic=abort','-C','opt-level=2',"$project/$dependency",'-o',"$out/$name")
    }
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/Gy',("/I$project"),"$project/arch/x86/kernel/drivers/input/ps2/runtime/route.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/interrupt.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/recovery.c", "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/runtime_test.c",
        "$project/test/kernel/drivers/input/ps2/controller/platform.c", "$project/kernel/firmware/acpi/madt/records.c",
        "$project/kernel/firmware/acpi/root.c","$project/lib/firmware/acpi/rsdp.c", "$project/lib/memory/address_ranges.c",
        ("/Fo$out/"),("/Fd$out/runtime.pdb"),("/Fe$out/runtime.exe"),'/link','/incremental:no','/opt:ref',
        "$out/kernel-firmware-acpi-rust-root-layout.obj","$out/lib-firmware-acpi-rust-rsdp-validation.obj",
        "$out/kernel-firmware-acpi-madt-rust-records-validation.obj", "$out/arch-x86-kernel-interrupts-apic-rust-registers-validation.obj")
    Invoke-Checked "$out/runtime.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/Gy','/DKERNEL_PS2_MOUSE=1',("/I$project"),"$project/arch/x86/kernel/drivers/input/ps2/runtime/route.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/interrupt.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/recovery.c", "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/runtime_test.c",
        "$project/test/kernel/drivers/input/ps2/controller/platform.c", "$project/kernel/firmware/acpi/madt/records.c",
        "$project/kernel/firmware/acpi/root.c","$project/lib/firmware/acpi/rsdp.c", "$project/lib/memory/address_ranges.c",
        ("/Fo$out/"),("/Fd$out/runtime.pdb"),("/Fe$out/runtime-mouse.exe"),'/link','/incremental:no','/opt:ref',
        "$out/kernel-firmware-acpi-rust-root-layout.obj","$out/lib-firmware-acpi-rust-rsdp-validation.obj",
        "$out/kernel-firmware-acpi-madt-rust-records-validation.obj", "$out/arch-x86-kernel-interrupts-apic-rust-registers-validation.obj")
    Invoke-Checked "$out/runtime-mouse.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/Gy',("/I$project"),
        "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/consumer_test.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/stream.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/recovery.c",
        "$project/kernel/console/line/line.c",("/Fo$out/"),("/Fd$out/consumer.pdb"),
        ("/Fe$out/consumer.exe"),'/link','/incremental:no','/opt:ref',"$out/decoder.obj")
    Invoke-Checked "$out/consumer.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/Gy','/DKERNEL_CONSOLE_SHELL=1',("/I$project"),
        "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/consumer_test.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/stream.c",
        "$project/arch/x86/kernel/drivers/input/ps2/runtime/consumer/recovery.c",
        "$project/kernel/console/line/line.c",("/Fo$out/"),("/Fd$out/consumer-video.pdb"),
        ("/Fe$out/consumer-video.exe"),'/link','/incremental:no','/opt:ref',"$out/decoder.obj")
    Invoke-Checked "$out/consumer-video.exe" @()
    $cppcheck="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") -DKERNEL_PS2_HOST_TEST "$project/test/kernel/drivers/input/ps2/decoder/abi_test.c" "$project/kernel/drivers/input/ps2/controller/controller.c" `
        "$project/kernel/drivers/input/ps2/controller/transport.c" "$project/kernel/drivers/input/ps2/controller/discovery.c" "$project/test/kernel/drivers/input/ps2/controller/transport_test.c" `
        "$project/test/kernel/drivers/input/ps2/controller/discovery_test.c" "$project/arch/x86/kernel/drivers/input/ps2/integration/native.c" `
        "$project/arch/x86/kernel/drivers/input/ps2/runtime" "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/runtime_test.c" `
        "$project/test/arch/x86/kernel/drivers/input/ps2/runtime/consumer_test.c" `
        "$project/test/kernel/input/keyboard/text/abi_test.c" `
        "$project/test/kernel/console/line/editor/abi_test.c" "$project/kernel/console/line/line.c" `
        "$project/test/kernel/shell/command/abi_test.c" "$project/kernel/shell/shell.c" "$project/kernel/shell/queue.c" `
        "$project/arch/x86/kernel/drivers/input/ps2/shell/task.c" `
        "$project/test/arch/x86/kernel/drivers/input/ps2/shell/bridge_test.c" `
        2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml=Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
Get-FileHash $source, (Join-Path $project 'test/kernel/drivers/input/ps2/decoder/set2_test.rs') |
    Select-Object Path,Hash | ConvertTo-Json | Set-Content "$out/sources.json"
"PASS: PS/2 decoder host evidence $out"
