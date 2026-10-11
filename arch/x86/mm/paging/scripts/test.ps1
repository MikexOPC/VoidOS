param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/paging-owned-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked tools ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "Tool failed: $Program ($LASTEXITCODE)" }
}

# ============ Rust debug release and Clippy ============

$test = "$project/test/kernel/mm/paging/rust/validation/entries_test.rs"
foreach ($mode in @('debug', 'release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        $test,'-o',"$out/entries-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/entries-$mode.exe" @()
}
$savedClippy = $env:CLIPPY_CONF_DIR
try {
    $env:CLIPPY_CONF_DIR = $PSScriptRoot
    Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings',
        '--emit=metadata',$test,'-o',"$out/entries.rmeta")
} finally {
    $env:CLIPPY_CONF_DIR = $savedClippy
}
$objects = @()
foreach ($source in @('arch/x86/mm/paging/rust/validation/entries.rs',
    'kernel/mm/rust/regions/validation.rs','kernel/mm/physical/rust/alignment/placement.rs',
    'kernel/drivers/video/framebuffer/rust/ffi/mode.rs')) {
    $object = "$out/" + ($source -replace '/', '-') + '.obj'
    Invoke-Checked 'rustc' @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',"$project/$source",'-o',$object)
    $objects += $object
}

# ============ supported x64 C ABI and ASan ============

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
    foreach ($source in @('arch/x86/mm/paging/inventory.c','arch/x86/mm/paging/authorization/validation.c',
        'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','lib/memory/address_ranges.c',
        'test/kernel/mm/paging/inventory_test.c','test/kernel/mm/paging/runner.c')) {
        $object = "$out/" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',("/I$project"),"$project/$source",("/Fo$object"),("/Fd$out/host.pdb"))
        $objects += $object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/host.exe")) +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    $cppcheck = "$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    & $cppcheck --enable=all --check-level=exhaustive --platform=win64 --std=c11 --xml --xml-version=2 `
        ("-I$project") "$project/arch/x86/mm/paging/inventory.c" `
        "$project/arch/x86/mm/paging/authorization/validation.c" `
        "$project/test/kernel/mm/paging/inventory_test.c" `
        "$project/test/kernel/mm/paging/runner.c" 2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml = Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: paging host evidence $out"
