param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror','-o',"$out/template.bin",
    "$project/arch/x86/kernel/cpu/percpu/startup/assembly/trampoline/entry.asm") -Log "$out/tools.log"
Invoke-OsChecked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",
    '-o',"$out/protocol.obj") -Log "$out/tools.log"
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/startup/transport/native/rearm.c',
        'arch/x86/kernel/cpu/percpu/startup/entry.c','arch/x86/kernel/cpu/percpu/startup/blob/package.c',
        'test/arch/x86/kernel/cpu/percpu/startup/transport/native/rearm.c')
    $objects = @("$out/protocol.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-OsChecked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/Zi',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source","/Fo$object","/Fd$out/test.pdb") -Log "$out/tools.log"
        $objects += $object
    }
    Invoke-OsChecked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX')) -Log "$out/tools.log"
    $cases = @('positive','high-index','last','owner','private','phase','future-phase','online',
        'tables','token','record','index','code-read','code-corrupt','pin','borrow','partial',
        'no-view','safe-after-write','readback','data-corrupt','post-tables')
    foreach ($case in $cases) { Invoke-OsChecked "$out/host.exe" @("$out/template.bin",$case) -Log "$out/tools.log" }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object {"$project/$_"})
} finally { Restore-Msvc $saved }
[ordered]@{ Cases = $cases.Count; CAsan = $true; RustPolicy = 'real'; MemoryIo = 'modeled' } |
    ConvertTo-Json | Set-Content "$out/results.json" -Encoding UTF8
"PASS: rearm host evidence $out"
