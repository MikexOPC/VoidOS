param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/ps2-mouse-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory $out -Force | Out-Null
$source = "$project/kernel/drivers/input/ps2/mouse/rust/packet/decode.rs"
$gateway = "$project/kernel/drivers/input/ps2/mouse/rust/ffi/gateway.rs"

# ============ checked tool execution ============

. (Join-Path $PSScriptRoot '../../../../../../scripts/lib/common.ps1')
function Invoke-Checked([string]$Program, [string[]]$Arguments) { Invoke-OsChecked $Program $Arguments "$out/tools.log" -Tee }

# ============ Rust transitions and scalar ABI ============

foreach ($mode in @('debug', 'release')) {
    $flags = @('--edition=2024', '--test', '-D', 'warnings', '-C', 'overflow-checks=yes',
        $source, '-o', "$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C', 'opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/$mode.exe" @()
}
Invoke-Checked 'clippy-driver' @('--edition=2024', '--test', '-D', 'warnings',
    '--emit=metadata', $source, '-o', "$out/packet.rmeta")
Invoke-Checked 'rustc' @('--edition=2024', '--crate-type=lib', '--emit=obj', '-D', 'warnings',
    '-C', 'panic=abort', '-C', 'opt-level=2', $gateway, '-o', "$out/mouse.obj")
Invoke-Checked 'clippy-driver' @('--edition=2024', '--crate-type=lib', '-D', 'warnings',
    '--emit=metadata', $gateway, '-o', "$out/gateway.rmeta")

# ============ C ABI and address sanitizer ============

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
    Invoke-Checked "$bin/cl.exe" @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi',
        '/fsanitize=address', '/MD', '/O2', ("/I$project"),
        "$project/kernel/drivers/input/ps2/mouse/decoder.c",
        "$project/test/kernel/drivers/input/ps2/mouse/abi_test.c",
        ("/Fo$out/"), ("/Fd$out/mouse.pdb"), ("/Fe$out/mouse.exe"),
        '/link', '/incremental:no', "$out/mouse.obj")
    Invoke-Checked "$out/mouse.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi',
        '/fsanitize=address', '/MD', '/O2', ("/I$project"),
        "$project/kernel/drivers/input/ps2/mouse/protocol.c",
        "$project/kernel/drivers/input/ps2/mouse/stream/control.c",
        "$project/kernel/drivers/input/ps2/controller/controller.c",
        "$project/kernel/drivers/input/ps2/controller/transport.c",
        "$project/test/kernel/drivers/input/ps2/mouse/controller/test.c",
        ("/Fo$out/"), ("/Fd$out/controller.pdb"), ("/Fe$out/controller.exe"),
        '/link', '/incremental:no')
    Invoke-Checked "$out/controller.exe" @()
    Invoke-Checked "$bin/cl.exe" @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi',
        '/fsanitize=address', '/MD', '/O2', ("/I$project"),
        "$project/kernel/drivers/input/ps2/mouse/decoder.c",
        "$project/arch/x86/kernel/drivers/input/ps2/mouse/task.c",
        "$project/test/kernel/drivers/input/ps2/mouse/runtime/task_test.c",
        ("/Fo$out/"), ("/Fd$out/task.pdb"), ("/Fe$out/task.exe"),
        '/link', '/incremental:no', "$out/mouse.obj")
    Invoke-Checked "$out/task.exe" @()
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}

# ============ static analysis ============

$cppcheck="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" "--quiet" --platform=win64 --std=c11 --xml --xml-version=2 `
    ("-I$project") "$project/kernel/drivers/input/ps2/mouse/decoder.c" `
    "$project/test/kernel/drivers/input/ps2/mouse/abi_test.c" `
    "$project/kernel/drivers/input/ps2/mouse/protocol.c" `
    "$project/kernel/drivers/input/ps2/mouse/stream/control.c" `
    "$project/kernel/drivers/input/ps2/controller/controller.c" `
    "$project/kernel/drivers/input/ps2/controller/transport.c" `
    "$project/arch/x86/kernel/drivers/input/ps2/mouse/session.c" `
    "$project/arch/x86/kernel/drivers/input/ps2/mouse/task.c" `
    "$project/test/kernel/drivers/input/ps2/mouse/runtime/task_test.c" `
    "$project/test/kernel/drivers/input/ps2/mouse/controller/test.c"
if ($LASTEXITCODE) { throw 'Cppcheck failed' }
[xml]$xml=Get-Content "$out/cppcheck.xml"
if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
    throw 'Blocking static diagnostics'
}
Get-FileHash $source, $gateway, "$project/kernel/drivers/input/ps2/mouse/decoder.c", `
    "$project/kernel/drivers/input/ps2/mouse/decoder.h", `
    "$project/test/kernel/drivers/input/ps2/mouse/abi_test.c", `
    "$project/test/kernel/drivers/input/ps2/mouse/rust/packet/decode_test.rs", `
    "$project/kernel/drivers/input/ps2/mouse/protocol.c", `
    "$project/kernel/drivers/input/ps2/mouse/controller.h", `
    "$project/kernel/drivers/input/ps2/controller/controller.c", `
    "$project/kernel/drivers/input/ps2/controller/controller.h", `
    "$project/kernel/drivers/input/ps2/controller/transport.c", `
    "$project/test/kernel/drivers/input/ps2/mouse/controller/test.c", `
    "$project/kernel/drivers/input/ps2/mouse/stream/control.c", `
    "$project/kernel/drivers/input/ps2/mouse/stream/control.h", `
    "$project/arch/x86/kernel/drivers/input/ps2/mouse/task.c", `
    "$project/arch/x86/kernel/drivers/input/ps2/mouse/session.c", `
    "$project/arch/x86/kernel/drivers/input/ps2/runtime/session.h", `
    "$project/test/kernel/drivers/input/ps2/mouse/runtime/task_test.c" |
    Select-Object Path,Hash | ConvertTo-Json | Set-Content "$out/sources.json"
"PASS: PS/2 mouse decoder host evidence $out"
