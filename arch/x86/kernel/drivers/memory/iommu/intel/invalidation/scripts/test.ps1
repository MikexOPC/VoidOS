param([string]$OutputDirectory='D:/OS/kernel/build/iommu-qi-host')
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../../../../../../../../..").Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc=Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk='C:/Program Files (x86)/Windows Kits/10'
$version=Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ checked execution ============

function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) {throw "Tool failed: $Program ($LASTEXITCODE)"}
}
try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $module='arch/x86/kernel/drivers/memory/iommu/intel/invalidation'
    $test='test/arch/x86/kernel/drivers/memory/iommu/intel/invalidation'
    $rust="$project/$module/rust"
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$rust/ffi/policy.rs",'-o',"$out/policy.obj")
    foreach ($mode in @('debug','release')) {
        $arguments=@('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',"$rust/policy/operations.rs",'-o',"$out/$mode.exe")
        if ($mode -eq 'release') {$arguments+=@('-C','opt-level=2')}
        Invoke-Checked rustc $arguments; Invoke-Checked "$out/$mode.exe" @()
    }
    foreach ($source in @('policy/operations.rs','ffi/policy.rs')) {
        Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','--emit=metadata','-D','warnings',"$rust/$source",'-o',"$out/check.rmeta")
    }
    foreach ($entry in @(@('kernel/mm/rust/regions/validation.rs','regions'),@('kernel/mm/physical/rust/alignment/placement.rs','placement'))) {
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$($entry[0])",'-o',"$out/$($entry[1]).obj")
    }
    $registers='arch/x86/kernel/drivers/memory/iommu/intel/registers'
    $domains='arch/x86/kernel/drivers/memory/iommu/intel/domains'
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$registers/rust/ffi/policy.rs",'-o',"$out/register-policy.obj")
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$domains/rust/ffi/policy.rs",'-o',"$out/domain-policy.obj")
    $sources=@("$module/lifecycle.c","$module/validation.c","$module/queue.c","$module/memory.c",
        "$registers/lifecycle.c","$registers/preflight.c","$registers/tables.c","$registers/access.c",
        "$domains/lifecycle.c","$domains/planning.c","$domains/construction.c","$domains/verification.c",
        'arch/x86/mm/paging/iommu/iommu_registry.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c',
        'lib/memory/address_ranges.c','test/arch/x86/kernel/drivers/memory/iommu/intel/registers/mock.c',"$test/model.c","$test/cases.c")
    $paths=@($sources | ForEach-Object {"$project/$_"})
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    $compiled=@()
    foreach ($source in $sources) {
        $object="$out/"+$source.Replace('/','-').Replace('.c','.obj')
        Invoke-Checked "$bin/cl.exe" ($flags+@($(if ($source -eq 'kernel/mm/physical/allocator.c') {'/Dkernel_physical_pin=qi_real_pin'} else {'/DQI_HOST_TEST=1'}),'/c',"$project/$source","/Fo$object","/Fd$out/host.pdb"))
        $compiled+=$object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/MD')+$compiled+@("/Fe$out/host.exe",'/link','/incremental:no','/DEBUG','/WX',
        "$out/policy.obj","$out/regions.obj","$out/placement.obj","$out/register-policy.obj","$out/domain-policy.obj"))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Checked "$bin/cl.exe" ($flags+@("$project/$module/backend/cpu.c","$project/$test/cpu_test.c","/Fo$out/","/Fd$out/cpu.pdb","/Fe$out/cpu.exe",'/link','/incremental:no','/DEBUG','/WX',"$out/policy.obj"))
    Invoke-Checked "$out/cpu.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $paths+=@("$project/$module/backend/cpu.c","$project/$module/backend/mmio.c")
    $arguments=@('--enable=all','--check-level=exhaustive','--platform=win64','--std=c11','--xml',
        ('"-I'+$project+'"'))+@($paths | ForEach-Object {'"'+$_+'"'})
    $checked=Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) {throw 'Cppcheck failed'}
    [xml]$report=Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {throw 'Blocking static diagnostics'}
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: VT-d QI/coherency evidence $out"
