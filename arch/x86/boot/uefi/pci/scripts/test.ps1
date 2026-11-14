param([string]$OutputDirectory = 'D:/OS/kernel/build/virtio-uefi-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path ($_.FullName+'/ucrt/stddef.h')} |
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
    Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/discovery/rust/ffi/gateway.rs",
        '-o',"$out/gateway.obj")
    $sources = @('arch/x86/boot/uefi/pci/acquire.c','arch/x86/boot/uefi/pci/descriptor.c',
        'kernel/drivers/video/virtio/discovery/configuration/read.c',
        'kernel/drivers/video/virtio/discovery/pci.c','kernel/drivers/video/virtio/discovery/handoff.c',
        'lib/memory/address_ranges.c')
    $paths = @($sources | ForEach-Object {"$project/$_"})
    Invoke-Checked "$bin/cl.exe" (@('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project")+$paths+@("$project/test/arch/x86/boot/uefi/pci/acquire_test.c",
        "/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',"$out/gateway.obj"))
    Invoke-Checked "$out/host.exe" @()
    $checker=(Resolve-OsCppcheck)
    $arguments = @('--enable=all',"--check-level=$OsCppcheckLevel","-j$OsCppcheckJobs","--quiet",'--platform=win64','--std=c11','--xml',
        '"-D_M_X64=100"',('"-I'+$project+'"')) + @($paths | ForEach-Object {'"'+$_+'"'}) +
        @('"'+$project+'/test/arch/x86/boot/uefi/pci/acquire_test.c"')
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
"PASS: UEFI PCI acquisition evidence $out"
