param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/framebuffer-owned-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked tools ============

. (Join-Path $PSScriptRoot '../../../../../../../scripts/lib/common.ps1')

# ============ Rust debug release and Clippy ============

$savedClippy = $env:CLIPPY_CONF_DIR
try {
    $env:CLIPPY_CONF_DIR = $PSScriptRoot
foreach ($component in @('tables','pixels','drawing')) {
$test = "$project/arch/x86/kernel/drivers/video/framebuffer/rust/tables/contents.rs"
if ($component -eq 'pixels') { $test = "$project/kernel/drivers/video/framebuffer/rust/pixel/validation.rs" }
if ($component -eq 'drawing') { $test = "$project/arch/x86/kernel/drivers/video/framebuffer/rust/drawing/validation.rs" }
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
    'arch/x86/kernel/drivers/video/framebuffer/rust/drawing/validation.rs',
    'arch/x86/kernel/interrupts/apic/rust/profile/validation.rs',
    'arch/x86/mm/paging/rust/validation/entries.rs',
    'kernel/mm/rust/regions/validation.rs','kernel/mm/physical/rust/alignment/placement.rs',
    'kernel/drivers/video/virtio/discovery/rust/ffi/gateway.rs','kernel/drivers/video/virtio/mapping/rust/ffi/pages.rs',
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
        'arch/x86/mm/paging/iommu/iommu_registry.c','arch/x86/mm/paging/surface/registry.c','arch/x86/mm/paging/dma/dma_registry.c','arch/x86/mm/paging/heap/heap_registry.c','arch/x86/mm/paging/gpu/authority.c','arch/x86/mm/paging/gpu/validation.c',
        'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
        'kernel/drivers/video/virtio/mapping/plan.c',
        'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','lib/memory/address_ranges.c',
        'kernel/drivers/video/framebuffer/handoff.c','kernel/drivers/video/framebuffer/mapping.c',
        'arch/x86/kernel/drivers/video/framebuffer/authority.c',
        'arch/x86/kernel/drivers/video/framebuffer/preflight.c',
        'arch/x86/kernel/drivers/video/framebuffer/construction.c',
        'arch/x86/kernel/drivers/video/framebuffer/lifecycle.c',
        'arch/x86/kernel/drivers/video/framebuffer/pixels/write.c','arch/x86/kernel/drivers/video/framebuffer/pixels/transaction.c',
        'arch/x86/kernel/drivers/video/framebuffer/drawing/draw.c','arch/x86/kernel/drivers/video/framebuffer/drawing/cursor.c',
        'kernel/console/video/pointer/shape.c',
        'arch/x86/kernel/drivers/video/framebuffer/font/font.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/pixels/write_test.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/pixels/pointer_test.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/lifecycle/platform.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/lifecycle/cases.c')) {
        $object = "$out/" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',("/I$project"),"$project/$source",("/Fo$object"),("/Fd$out/host.pdb"))
        $objects += $object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/host.exe")) +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    $cppcheck = "$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $arguments = @('--enable=all','--check-level=exhaustive','--platform=win64','--std=c11','--xml','--xml-version=2',
        ('"-I'+$project+'"')) + @(
        'arch/x86/kernel/drivers/video/framebuffer/lifecycle.c',
        'arch/x86/kernel/drivers/video/framebuffer/construction.c',
        'arch/x86/kernel/drivers/video/framebuffer/preflight.c',
        'arch/x86/kernel/drivers/video/framebuffer/authority.c',
        'arch/x86/kernel/drivers/video/framebuffer/native/access.c',
        'arch/x86/kernel/drivers/video/framebuffer/integration/kernel.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/lifecycle/platform.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/lifecycle/cases.c',
        'arch/x86/kernel/drivers/video/framebuffer/pixels/write.c',
        'arch/x86/kernel/drivers/video/framebuffer/pixels/transaction.c',
        'arch/x86/kernel/drivers/video/framebuffer/drawing/draw.c',
        'arch/x86/kernel/drivers/video/framebuffer/drawing/cursor.c',
        'kernel/console/video/pointer/shape.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/pixels/pointer_test.c',
        'arch/x86/kernel/drivers/video/framebuffer/font/font.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/drawing/probe.c',
        'arch/x86/kernel/drivers/video/framebuffer/pixels/native.c',
        'test/arch/x86/kernel/drivers/video/framebuffer/pixels/write_test.c' |
        ForEach-Object {'"'+$project+'/'+$_+'"'})
    $checked = Start-Process $cppcheck -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) { throw 'Cppcheck failed' }
    [xml]$xml = Get-Content "$out/cppcheck.xml"
    if (@($xml.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: framebuffer host evidence $out"
