param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-admission-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ snapshot policy and actual atomic boundary ============

foreach ($mode in @('debug','release')) {
    $arguments = @('--edition=2024','--test','-D','warnings',
        "$project/test/arch/x86/kernel/cpu/percpu/admission/rust/snapshot/cases.rs",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $arguments += @('-C','opt-level=2') }
    Invoke-OsChecked rustc $arguments -Log "$out/tools.log"
    Invoke-OsChecked "$out/$mode.exe" @() -Log "$out/tools.log"
}
$objects = @()
foreach ($source in @('arch/x86/kernel/cpu/percpu/admission/rust/snapshot/policy.rs',
    'kernel/sync/rust/atomics/operations.rs')) {
    $object = "$out/" + ($source -replace '/','-') + '.obj'
    Invoke-OsChecked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
        '-C','panic=abort','-C','opt-level=2',"$project/$source",'-o',$object) -Log "$out/tools.log"
    $objects += $object
}
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/admission/group.c','kernel/sync/lock.c','kernel/time/clock.c',
        'test/arch/x86/kernel/cpu/percpu/admission/publication.c')
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/Zi',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source","/Fo$object","/Fd$out/host.pdb") -Log "$out/tools.log"
        $objects += $object
    }
    Invoke-OsChecked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX')) -Log "$out/tools.log"
    foreach ($repeat in 1..10) {
        foreach ($case in @('small','full')) { Invoke-OsChecked "$out/host.exe" @($case) -Log "$out/tools.log" }
    }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
[ordered]@{Status='IMMUTABLE_GROUP_HOST_PASSED'; ModuleReady=$false; ApStarted=0; RustCases=5;
    RustModes=@('debug','release'); HostProcesses=20; ReadersPerProcess=4; CAsan=$true; RustAsan=$false;
    NativeAdmission=$false; Coverage='N/D'} | ConvertTo-Json | Set-Content "$out/result.json" -Encoding UTF8
"PASS: admission publication host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
