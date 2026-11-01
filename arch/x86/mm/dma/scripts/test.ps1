param([string]$OutputDirectory='D:/OS/kernel/build/dma-arena-host')
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../../../../..").Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc=Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk='C:/Program Files (x86)/Windows Kits/10'
$version=Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../scripts/lib/common.ps1')

# ============ arena owner and registry: real allocator, modeled CPU/MMU ============

try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    foreach ($entry in @(@('kernel/mm/rust/regions/validation.rs','regions'),@('kernel/mm/physical/rust/alignment/placement.rs','placement'))) {
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',
            "$project/$($entry[0])",'-o',"$out/$($entry[1]).obj")
    }
    $sources=@('arch/x86/mm/dma/arena.c','arch/x86/mm/paging/dma/dma_registry.c','arch/x86/mm/paging/heap/heap_registry.c','arch/x86/kernel/cpu/percpu/resources/paging/registry.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c','kernel/mm/physical/allocation/state.c','kernel/mm/physical/allocation/guard.c','kernel/mm/physical/allocation/queries.c',
        'lib/memory/address_ranges.c','test/arch/x86/mm/dma/mock.c','test/arch/x86/mm/dma/cases.c')
    $paths=@($sources | ForEach-Object {"$project/$_"})
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    . (Join-Path $project 'kernel/mm/physical/scripts/host.ps1')
    $syncObjects = @(New-PhysicalHostSync $project $out)
    Invoke-Checked "$bin/cl.exe" ($flags+$paths+@("/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',
        "$out/regions.obj","$out/placement.obj") + $syncObjects)
    Invoke-Checked "$out/host.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $arguments=@('--enable=all',"--check-level=$OsCppcheckLevel","-j$OsCppcheckJobs","--quiet",'--platform=win64','--std=c11','--xml',
        ('"-I'+$project+'"'))+@($paths | ForEach-Object {'"'+$_+'"'})
    $checked=Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) {throw 'Cppcheck failed'}
    [xml]$report=Get-Content "$out/cppcheck.xml"
    $blocking=@($report.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'})
    if ($blocking.Count) {$blocking | ForEach-Object {"$($_.id): $($_.msg)"}; throw 'Blocking static diagnostics'}
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: DMA arena host evidence $out"
