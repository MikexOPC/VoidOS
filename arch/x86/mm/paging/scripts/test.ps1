param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $project ('kernel/build/paging-owned-' + [guid]::NewGuid().ToString('N'))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ checked tools ============

. (Join-Path $PSScriptRoot '../../../../../scripts/lib/common.ps1')

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
        'arch/x86/mm/paging/iommu/iommu_registry.c','arch/x86/mm/paging/surface/registry.c','arch/x86/mm/paging/dma/dma_registry.c','arch/x86/mm/paging/heap/heap_registry.c','arch/x86/kernel/cpu/percpu/resources/paging/registry.c','arch/x86/mm/paging/gpu/authority.c','arch/x86/mm/paging/gpu/validation.c',
        'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
        'kernel/drivers/video/virtio/mapping/plan.c',
        'kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c','lib/memory/address_ranges.c',
        'test/kernel/mm/paging/inventory_test.c','test/kernel/mm/paging/fixture.c','test/kernel/mm/paging/device_test.c','test/kernel/mm/paging/runner.c')) {
        $object = "$out/" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',("/I$project"),"$project/$source",("/Fo$object"),("/Fd$out/host.pdb"))
        $objects += $object
    }
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $objects += @(New-PhysicalHostSync $project $out)
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/host.exe")) +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    $differentialObjects = @()
    foreach ($source in @('kernel/mm/inventory.c','kernel/mm/physical/allocation/state.c',
        'test/kernel/mm/physical/validation/reference.c','test/kernel/mm/physical/validation/differential.c',
        'test/kernel/mm/inventory/reference.c',
        'test/kernel/mm/inventory/differential.c','test/arch/x86/mm/paging/authorization/aliases.c')) {
        $object = "$out/differential-" + ($source -replace '/', '-') + '.obj'
        Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/c',("/I$project"),"$project/$source",("/Fo$object"),("/Fd$out/differential.pdb"))
        $differentialObjects += $object
    }
    $regions = "$out/kernel-mm-rust-regions-validation.rs.obj"
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/fsanitize=address','/MD',("/Fe$out/differential.exe")) +
        $differentialObjects + @($regions,"$out/kernel-mm-physical-rust-alignment-placement.rs.obj",'/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/differential.exe" @()
    $cppcheck = (Resolve-OsCppcheck)
    $arguments = @('--enable=all',"--check-level=$OsCppcheckLevel","-j$OsCppcheckJobs","--quiet",'--platform=win64','--std=c11','--xml','--xml-version=2',
        ('"-I'+$project+'"')) + @('arch/x86/mm/paging/inventory.c',
        'arch/x86/mm/paging/authorization/validation.c','test/kernel/mm/paging/inventory_test.c','test/kernel/mm/paging/fixture.c','test/kernel/mm/paging/device_test.c',
        'test/kernel/mm/paging/runner.c','kernel/mm/inventory.c','kernel/mm/physical/allocation/state.c',
        'test/kernel/mm/inventory/reference.c','test/kernel/mm/inventory/differential.c',
        'test/kernel/mm/physical/validation/reference.c','test/kernel/mm/physical/validation/differential.c',
        'test/arch/x86/mm/paging/authorization/aliases.c' | ForEach-Object {'"'+$project+'/'+$_+'"'})
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
"PASS: paging host evidence $out"
