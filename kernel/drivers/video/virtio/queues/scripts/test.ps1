param([string]$OutputDirectory='D:/OS/kernel/build/virtio-queues-host')
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../../../../../..").Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc=Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk='C:/Program Files (x86)/Windows Kits/10'
$version=Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../../scripts/lib/common.ps1')
try {
    if (!$vc -or !$version) {throw 'MSVC/SDK unavailable'}
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    $rust="$project/kernel/drivers/video/virtio/queues/rust"
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$rust/ffi/validation.rs",'-o',"$out/policy.obj")
    foreach ($mode in @('debug','release')) {
        $arguments=@('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',"$rust/policy/validation.rs",'-o',"$out/$mode.exe")
        if ($mode -eq 'release') {$arguments+=@('-C','opt-level=2')}
        Invoke-Checked rustc $arguments
        Invoke-Checked "$out/$mode.exe" @()
    }
    foreach ($source in @('policy/validation.rs','ffi/validation.rs')) {
        Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','--emit=metadata','-D','warnings',"$rust/$source",'-o',"$out/check.rmeta")
    }
    $sources=@('kernel/drivers/video/virtio/queues/setup.c','kernel/drivers/video/virtio/queues/exchange.c',
        'kernel/drivers/video/virtio/queues/response.c','lib/memory/address_ranges.c','test/kernel/drivers/video/virtio/queues/host.c')
    $paths=@($sources | ForEach-Object {"$project/$_"})
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project")+$paths+@("/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',"$out/policy.obj"))
    Invoke-Checked "$out/host.exe" @()
    $native=@("$project/arch/x86/kernel/drivers/video/virtio/queues/native.c",
        "$project/test/arch/x86/kernel/drivers/video/virtio/queues/native_test.c","$project/lib/memory/address_ranges.c")
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project")+$native+@("/Fo$out/","/Fd$out/native.pdb","/Fe$out/native.exe",'/link','/incremental:no',"$out/policy.obj"))
    Invoke-Checked "$out/native.exe" @()
    $lifecycle=@("$project/test/arch/x86/kernel/drivers/video/virtio/queues/probe.c",
        "$project/test/arch/x86/kernel/drivers/video/virtio/queues/lifecycle_test.c")
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project")+$lifecycle+@("/Fo$out/","/Fd$out/lifecycle.pdb","/Fe$out/lifecycle.exe",'/link','/incremental:no'))
    Invoke-Checked "$out/lifecycle.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $paths+=$native+$lifecycle+@("$project/arch/x86/kernel/drivers/video/virtio/queues/pci.c","$project/arch/x86/kernel/drivers/video/virtio/queues/access.c")
    $arguments=@('--enable=all',"--check-level=$OsCppcheckLevel","-j$OsCppcheckJobs","--quiet",'--platform=win64','--std=c11','--xml',
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
"PASS: VirtIO queues host evidence $out"
