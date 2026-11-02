param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-runtime-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual runtime orchestration, modeled hardware dependencies ============

$saved = Use-Msvc
try {
    $source = "$project/test/arch/x86/kernel/cpu/percpu/startup/runtime/runtime.c"
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
        '/MD','/O2','/UNDEBUG',"/I$project",$source,"/Fe$out/runtime.exe","/Fo$out/runtime.obj","/Fd$out/runtime.pdb",
        '/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/runtime.exe" @()
    Invoke-Cppcheck $project $out @($source,"$project/arch/x86/kernel/cpu/percpu/startup/runtime/runtime.c")
} finally { Restore-Msvc $saved }
'PASS: AP runtime orchestration host'
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
