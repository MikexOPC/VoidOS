param([string]$OutputDirectory = 'D:/OS/kernel/build/virtio-pci-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$rust = "$project/kernel/drivers/video/virtio/discovery/rust"
$python = "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"

# ============ checked execution ============

. (Join-Path $PSScriptRoot '../../../../../../scripts/lib/common.ps1')

Invoke-Checked $python @('-B',"$PSScriptRoot/structure.py")
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings','-C','overflow-checks=yes',
        "$rust/pci/validation.rs",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/$mode.exe" @()
}
Invoke-Checked clippy-driver @('--edition=2024','--test','-D','warnings','--emit=metadata',
    "$rust/pci/validation.rs",'-o',"$out/validation.rmeta")
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','overflow-checks=yes','-C','opt-level=2',"$rust/ffi/gateway.rs",'-o',"$out/gateway.obj")
Invoke-Checked clippy-driver @('--edition=2024','--crate-type=lib','-D','warnings','--emit=metadata',
    "$rust/ffi/gateway.rs",'-o',"$out/gateway.rmeta")
$vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$version = Get-ChildItem "$sdk/Include" -Directory | Where-Object { Test-Path ($_.FullName+'/ucrt/stddef.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB; $savedPath=$env:PATH
try {
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $bin=$vc.FullName+'/bin/Hostx64/x64'; $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$vc.FullName+'/include;'+$sdk+'/Include/'+$version.Name+'/ucrt;'+
        $sdk+'/Include/'+$version.Name+'/um;'+$sdk+'/Include/'+$version.Name+'/shared'
    $env:LIB=$vc.FullName+'/lib/x64;'+$sdk+'/Lib/'+$version.Name+'/ucrt/x64;'+$sdk+'/Lib/'+$version.Name+'/um/x64'
    Invoke-Checked "$bin/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2',"/I$project","$project/kernel/drivers/video/virtio/discovery/pci.c",
        "$project/kernel/drivers/video/virtio/discovery/configuration/read.c",
        "$project/test/kernel/drivers/video/virtio/discovery/configuration/read_test.c",
        "$project/lib/memory/address_ranges.c","$project/test/kernel/drivers/video/virtio/discovery/abi_test.c",
        "/Fo$out/","/Fd$out/host.pdb","/Fe$out/host.exe",'/link','/incremental:no',"$out/gateway.obj")
    Invoke-Checked "$out/host.exe" @()
    $checker="$project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $checkerArguments = @('--enable=all',"--check-level=$OsCppcheckLevel","-j$OsCppcheckJobs","--quiet",'--platform=win64','--std=c11',
        '--xml','--xml-version=2',('"-I'+$project+'"'),
        ('"'+$project+'/kernel/drivers/video/virtio/discovery/pci.c"'),
        ('"'+$project+'/test/kernel/drivers/video/virtio/discovery/abi_test.c"'))
    $checkerArguments += @(('"'+$project+'/kernel/drivers/video/virtio/discovery/configuration/read.c"'),(' "'+$project+'/test/kernel/drivers/video/virtio/discovery/configuration/read_test.c"').Trim())
    $checked = Start-Process -FilePath $checker -ArgumentList $checkerArguments -WindowStyle Hidden `
        -Wait -PassThru -RedirectStandardError "$out/cppcheck.xml" -RedirectStandardOutput "$out/cppcheck.log"
    if ($checked.ExitCode) { throw 'Cppcheck failed' }
    [xml]$report=Get-Content "$out/cppcheck.xml"
    if (@($report.results.errors.error | Where-Object {$_.severity -in 'error','warning','performance','portability'}).Count) {
        throw 'Blocking static diagnostics'
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib; $env:PATH=$savedPath
}
"PASS: VirtIO GPU PCI host evidence $out"
