param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-return-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ safe Rust plan and C ABI ============

foreach ($mode in @('debug','release')) {
    $arguments = @('--edition=2024','--test','-D','warnings',
        "$project/test/arch/x86/kernel/cpu/percpu/startup/rust/return/cases.rs",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $arguments += @('-C','opt-level=2') }
    Invoke-OsChecked rustc $arguments -Log "$out/tools.log"
    Invoke-OsChecked "$out/$mode.exe" @() -Log "$out/tools.log"
}
Invoke-OsChecked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
    "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',"$out/protocol.obj") -Log "$out/tools.log"
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/startup/private/return.c','arch/x86/kernel/cpu/percpu/startup/entry.c',
        'test/arch/x86/kernel/cpu/percpu/startup/private/return/cases.c')
    $objects = @("$out/protocol.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/Zi',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source","/Fo$object","/Fd$out/test.pdb") -Log "$out/tools.log"
        $objects += $object
    }
    Invoke-OsChecked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX')) -Log "$out/tools.log"
    Invoke-OsChecked "$out/host.exe" @("$out/plan.bin") -Log "$out/tools.log"
    Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/GS-','/Zl',
        "/I$project",'/c',"$project/arch/x86/kernel/cpu/percpu/startup/private/return.c","/Fo$out/freestanding.obj") -Log "$out/tools.log"
    Invoke-OsChecked "$($saved.Bin)/dumpbin.exe" @('/symbols',"$out/freestanding.obj") -Log "$out/freestanding.log"
    $undefined = @(Get-Content "$out/freestanding.log" | Where-Object { $_ -match '\bUNDEF\b' })
    if ($undefined.Count -ne 1 -or $undefined[0] -notmatch '\| xs_return_valid$') { throw 'Unexpected C return runtime dependency' }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
[ordered]@{ Status='HOST_RETURN_PLAN_PASSED'; ModuleReady=$false; ApStarted=0; RustCases=5;
    RustModes=@('debug','release'); CAsan=$true; RustAsan=$false; Coverage='N/D' } |
    ConvertTo-Json | Set-Content "$out/result.json" -Encoding UTF8
"PASS: private return host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
