param([string]$OutputDirectory = 'D:/OS/kernel/build/virtio-mapping-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../../../scripts/lib/common.ps1')
try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/discovery/rust/ffi/gateway.rs",
        '-o',"$out/gateway.obj")
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/mapping/rust/ffi/pages.rs",'-o',"$out/pages.obj")
    foreach ($entry in @(@('kernel/mm/rust/regions/validation.rs','regions'),@('kernel/mm/physical/rust/alignment/placement.rs','placement'))) {
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$($entry[0])",'-o',"$out/$($entry[1]).obj")
    }
    $sources = @('kernel/drivers/video/virtio/mapping/plan.c',
        'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
        'lib/memory/address_ranges.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c',
        'arch/x86/mm/paging/iommu/iommu_registry.c','arch/x86/mm/paging/surface/registry.c','arch/x86/mm/paging/dma/dma_registry.c','arch/x86/mm/paging/heap/heap_registry.c','arch/x86/mm/paging/gpu/authority.c','arch/x86/mm/paging/gpu/validation.c',
        'arch/x86/kernel/drivers/video/virtio/preflight.c','arch/x86/kernel/drivers/video/virtio/construction.c',
        'arch/x86/kernel/drivers/video/virtio/lifecycle.c','test/arch/x86/kernel/drivers/video/virtio/mapping/mock.c')
    $paths = @($sources | ForEach-Object {"$project/$_"})
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project")+$paths+@("$project/test/arch/x86/kernel/drivers/video/virtio/mapping/cases.c",
        "/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',"$out/gateway.obj","$out/pages.obj","$out/regions.obj","$out/placement.obj"))
    Invoke-Checked "$out/host.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $arguments = @('--enable=all','--check-level=exhaustive','--platform=win64','--std=c11','--xml',
        '"-D_M_X64=100"',('"-I'+$project+'"')) + @($paths | ForEach-Object {'"'+$_+'"'}) +
        @('"'+$project+'/test/arch/x86/kernel/drivers/video/virtio/mapping/cases.c"')
    $checked = Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) {throw 'Cppcheck failed'}
    [xml]$report=Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: VirtIO mapping host evidence $out"
