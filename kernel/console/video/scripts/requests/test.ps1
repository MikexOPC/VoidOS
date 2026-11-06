param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../..").Path
. "$project/scripts/lib/common.ps1"
$out = if ($OutputDirectory) { $OutputDirectory } else {
    "$project/kernel/build/console-requests-" + [guid]::NewGuid().ToString('N')
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ Rust FFI and fixed ABI ============

$rustc = "$env:USERPROFILE/.cargo/bin/rustc.exe"
$gateway = "$project/kernel/console/video/rust/ffi/requests.rs"
Invoke-Checked "$env:USERPROFILE/.cargo/bin/clippy-driver.exe" @('--edition=2024',
    '--crate-type=lib','--emit=metadata','-D','warnings',$gateway,'-o',"$out/ffi.rmeta")
foreach ($profile in @('debug','release')) {
    $optimization = if ($profile -eq 'release') { 2 } else { 0 }
    Invoke-Checked $rustc @('--edition=2024','--crate-type=staticlib','-D','warnings',
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
    $sources = @('kernel/console/video/requests/queue.c','kernel/sync/lock.c','kernel/sync/acquire.c',
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
} finally { Restore-Msvc $saved }
"PASS: console request gateway evidence $out"
