param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/sync-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ Rust policy and atomic64 ============

$test = "$project/test/kernel/sync/rust/atomics/operations_test.rs"
foreach ($mode in @('debug', 'release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes', $test,'-o',"$out/rust-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/rust-$mode.exe" @()
}
Invoke-Checked clippy-driver @('--edition=2024','--test','-D','warnings','--emit=metadata', $test,'-o',"$out/tests.rmeta")
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/kernel/sync/rust/atomics/operations.rs",'-o',"$out/atomics.obj")

# ============ C host ASan and real thread contention ============

$saved = Use-Msvc
try {
    $objects = @("$out/atomics.obj")
    foreach ($source in @('kernel/sync/lock.c','kernel/sync/acquire.c','kernel/time/clock.c',
        'arch/x86/lib/synchronization/context.c','test/kernel/sync/context/provider.c',
        'test/kernel/sync/backend.c','test/kernel/sync/cases.c','test/kernel/sync/threads.c','test/kernel/sync/main.c')) {
        $object = "$out/" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',"/I$project","$project/$source","/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/fsanitize=address','/MD',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    foreach ($threads in @(2, 4, 8)) { Invoke-Checked "$out/host.exe" @('threads', [string]$threads) }
    $python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    Invoke-Checked $python @('-B',"$project/test/kernel/sync/scripts/fatal.py","$out/host.exe")
    $inputs = @('kernel/sync/lock.c','kernel/sync/acquire.c','arch/x86/lib/synchronization/context.c',
        'test/kernel/sync/context/provider.c','test/kernel/sync/cases.c','test/kernel/sync/threads.c') |
        ForEach-Object { Join-Path $project $_ }
    Invoke-CppcheckRaw "$out/cppcheck.xml" --enable=all "--check-level=$OsCppcheckLevel" "-j$OsCppcheckJobs" `
        --quiet --platform=win64 --std=c11 --xml --xml-version=2 "-I$project" @inputs
    if ($LASTEXITCODE) { throw 'Cppcheck execution failed' }
    [xml]$report = Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object { $_.severity -in 'error','warning','performance','portability' }).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    Restore-Msvc $saved
}
"PASS: synchronization host evidence $out"
