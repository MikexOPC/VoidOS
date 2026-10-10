param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/framebuffer-owned-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked tools ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "Tool failed: $Program ($LASTEXITCODE)" }
}

# ============ Rust debug release and Clippy ============

$savedClippy = $env:CLIPPY_CONF_DIR
try {
    $env:CLIPPY_CONF_DIR = $PSScriptRoot
foreach ($component in @('tables','pixels')) {
$test = "$project/arch/x86/kernel/drivers/video/framebuffer/rust/tables/contents.rs"
if ($component -eq 'pixels') { $test = "$project/kernel/drivers/video/framebuffer/rust/pixel/validation.rs" }
foreach ($mode in @('debug', 'release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        $test,'-o',"$out/$component-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked 'rustc' $flags
    Invoke-Checked "$out/$component-$mode.exe" @()
}
    Invoke-Checked 'clippy-driver' @('--edition=2024','--test','-D','warnings',
        '--emit=metadata',$test,'-o',"$out/$component.rmeta")
}
} finally {
    $env:CLIPPY_CONF_DIR = $savedClippy
}
$objects = @()
foreach ($source in @('arch/x86/kernel/drivers/video/framebuffer/rust/tables/contents.rs',
    'arch/x86/kernel/interrupts/apic/rust/profile/validation.rs',
    'arch/x86/mm/paging/rust/validation/entries.rs',
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
        'kernel/drivers/video/framebuffer/handoff.c','kernel/drivers/video/framebuffer/mapping.c',
        'arch/x86/kernel/drivers/video/framebuffer/authority.c',
        'arch/x86/kernel/drivers/video/framebuffer/preflight.c',
        'arch/x86/kernel/drivers/video/framebuffer/construction.c',
        'arch/x86/kernel/drivers/video/framebuffer/lifecycle.c',
        'arch/x86/kernel/drivers/video/framebuffer/pixels/write.c',
        'arch/x86/kernel/drivers/video/framebuffer/test/pixels/write_test.c',
        'arch/x86/kernel/drivers/video/framebuffer/test/lifecycle/platform.c',
        'arch/x86/kernel/drivers/video/framebuffer/test/lifecycle/cases.c')) {
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
        ("-I$project") "$project/arch/x86/kernel/drivers/video/framebuffer/lifecycle.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/construction.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/preflight.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/authority.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/native/access.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/integration/kernel.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/test/lifecycle/platform.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/test/lifecycle/cases.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/pixels/write.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/pixels/native.c" `
        "$project/arch/x86/kernel/drivers/video/framebuffer/test/pixels/write_test.c" 2> "$out/cppcheck.xml"
    if ($LASTEXITCODE) { throw 'Cppcheck failed' }
    [xml]$xml = Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: framebuffer host evidence $out"
