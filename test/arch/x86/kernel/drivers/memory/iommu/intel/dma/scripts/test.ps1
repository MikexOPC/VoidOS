param([string]$OutputDirectory='D:/OS/kernel/build/iommu-dma-host')
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../../../../../../../../../..").Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc=Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk='C:/Program Files (x86)/Windows Kits/10'
$version=Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../../../../../../scripts/lib/common.ps1')

# ============ edu model, verifier syntax and static analysis ============

try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $test='test/arch/x86/kernel/drivers/memory/iommu/intel/dma'
    $sources=@("$test/edu.c","$test/model.c","$test/cases.c")
    $flags=@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address','/MD','/O2',"/I$project")
    $compiled=@()
    foreach ($source in $sources) {
        $object="$out/"+$source.Replace('/','-').Replace('.c','.obj')
        Invoke-Checked "$bin/cl.exe" ($flags+@('/c',"$project/$source","/Fo$object","/Fd$out/host.pdb"))
        $compiled+=$object
    }
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/MD')+$compiled+@("/Fe$out/host.exe",'/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @()
    $python=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    Invoke-Checked $python @('-I','-B','-c',"import ast,sys; ast.parse(open(sys.argv[1]).read())","$project/$test/scripts/firmware.py")
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $paths=@("$test/edu.c","$test/model.c","$test/cases.c","$test/integration/probe.c","$test/integration/phases.c") |
        ForEach-Object {"$project/$_"}
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
"PASS: VT-d DMA evidence $out"
