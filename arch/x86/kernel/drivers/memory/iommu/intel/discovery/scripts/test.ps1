param([string]$OutputDirectory='D:/OS/kernel/build/iommu-discovery-host')
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
try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $module='arch/x86/kernel/drivers/memory/iommu/intel/discovery'
    $rust="$project/$module/rust"
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$rust/ffi/fields.rs",'-o',"$out/policy.obj")
    foreach ($mode in @('debug','release')) {
        $arguments=@('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',"$rust/policy/fields.rs",'-o',"$out/$mode.exe")
        if ($mode -eq 'release') {$arguments+=@('-C','opt-level=2')}
        Invoke-Checked rustc $arguments; Invoke-Checked "$out/$mode.exe" @()
    }
    foreach ($source in @('policy/fields.rs','ffi/fields.rs')) {
        Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','--emit=metadata','-D','warnings',"$rust/$source",'-o',"$out/check.rmeta")
    }
    $sources=@("$module/table.c","$module/find.c",'lib/memory/address_ranges.c',
        'test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/host.c')
    $paths=@($sources | ForEach-Object {"$project/$_"})
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    Invoke-Checked "$bin/cl.exe" ($flags+$paths+@("/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',"$out/policy.obj"))
    Invoke-Checked "$out/host.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $paths+=@("$project/test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/probe.c")
    $arguments=@('--enable=all','--check-level=exhaustive','--platform=win64','--std=c11','--xml',
        ('"-I'+$project+'"'))+@($paths | ForEach-Object {'"'+$_+'"'})
    $checked=Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) {throw 'Cppcheck failed'}
    [xml]$report=Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: DMAR host evidence $out"
