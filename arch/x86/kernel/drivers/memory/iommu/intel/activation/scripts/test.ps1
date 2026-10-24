param([string]$OutputDirectory='D:/OS/kernel/build/iommu-activation-host')
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

. (Join-Path $PSScriptRoot '../../../../../../../../../scripts/lib/common.ps1')

# ============ Rust policy, C model and static analysis ============

try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $intel='arch/x86/kernel/drivers/memory/iommu/intel'
    $module="$intel/activation"; $test='test/'+$module; $rust="$project/$module/rust"
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$rust/ffi/policy.rs",'-o',"$out/activation-policy.obj")
    foreach ($mode in @('debug','release')) {
        $arguments=@('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',"$rust/policy/commands.rs",'-o',"$out/$mode.exe")
        if ($mode -eq 'release') {$arguments+=@('-C','opt-level=2')}
        Invoke-Checked rustc $arguments; Invoke-Checked "$out/$mode.exe" @()
    }
    foreach ($source in @('policy/commands.rs','ffi/policy.rs')) {
        Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','--emit=metadata','-D','warnings',"$rust/$source",'-o',"$out/check.rmeta")
    }
    foreach ($entry in @(@('kernel/mm/rust/regions/validation.rs','regions'),@('kernel/mm/physical/rust/alignment/placement.rs','placement'),
        @("$intel/registers/rust/ffi/policy.rs",'register-policy'),@("$intel/domains/rust/ffi/policy.rs",'domain-policy'),
        @("$intel/invalidation/rust/ffi/policy.rs",'qi-policy'))) {
        Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort','-C','opt-level=2',"$project/$($entry[0])",'-o',"$out/$($entry[1]).obj")
    }
    $sources=@("$module/sequence.c","$module/faults.c",
        "$intel/invalidation/lifecycle.c","$intel/invalidation/validation.c","$intel/invalidation/queue.c","$intel/invalidation/memory.c",
        "$intel/registers/lifecycle.c","$intel/registers/preflight.c","$intel/registers/tables.c","$intel/registers/access.c",
        "$intel/domains/lifecycle.c","$intel/domains/planning.c","$intel/domains/construction.c","$intel/domains/verification.c",
        'arch/x86/mm/paging/iommu/iommu_registry.c','kernel/mm/inventory.c','kernel/mm/physical/allocator.c','lib/memory/address_ranges.c',
        "test/$intel/registers/mock.c","test/$intel/invalidation/model.c","$test/model.c","$test/cases.c")
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    $compiled=@()
    foreach ($source in $sources) {
        $object="$out/"+$source.Replace('/','-').Replace('.c','.obj')
        $define=$(if ($source -eq 'kernel/mm/physical/allocator.c') {'/Dkernel_physical_pin=qi_real_pin'} else {'/DQI_HOST_TEST=1'})
        Invoke-Checked "$bin/cl.exe" ($flags+@($define,'/c',"$project/$source","/Fo$object","/Fd$out/host.pdb"))
        $compiled+=$object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/MD')+$compiled+@("/Fe$out/host.exe",'/link','/incremental:no','/DEBUG','/WX',
        "$out/activation-policy.obj","$out/qi-policy.obj","$out/regions.obj","$out/placement.obj","$out/register-policy.obj","$out/domain-policy.obj"))
    Invoke-Checked "$out/host.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $paths=@("$module/sequence.c","$module/faults.c","$module/backend/mmio.c","$test/model.c","$test/cases.c",
        "test/$intel/activation/integration/probe.c") | ForEach-Object {"$project/$_"}
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
"PASS: VT-d activation evidence $out"
