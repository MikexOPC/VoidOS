param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/percpu-panic-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ actual Rust coordinator and concurrent host tests ============

foreach ($mode in @('debug','release')) {
    $options = @('--edition=2024','--test','-D','warnings',
        "$project/test/arch/x86/kernel/cpu/percpu/panic/rust/coordinator/cases.rs",'-o',"$out/rust-$mode.exe")
    if ($mode -eq 'release') { $options += @('-C','opt-level=2') }
    Invoke-Checked rustc $options
    Invoke-Checked "$out/rust-$mode.exe" @()
}
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/arch/x86/kernel/cpu/percpu/panic/rust/coordinator/api.rs",'-o',"$out/coordinator.obj")
$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/cpu/percpu/panic/records.c','test/arch/x86/kernel/cpu/percpu/panic/records.c') |
        ForEach-Object { Join-Path $project $_ }
    $objects = @("$out/coordinator.obj")
    foreach ($source in $sources) {
        $object = "$out/" + (($source.Substring($project.Length + 1) -replace '[\\/]','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$source,"/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('kernel32.lib','/link','/incremental:no','/DEBUG','/WX'))
    foreach ($run in 1..10) { Invoke-Checked "$out/host.exe" @() }
    $transportSources = @('arch/x86/kernel/cpu/percpu/panic/transport/wait.c','test/arch/x86/kernel/cpu/percpu/panic/transport.c') |
        ForEach-Object { Join-Path $project $_ }
    $transportObjects = @("$out/coordinator.obj")
    foreach ($source in $transportSources) {
        $object = "$out/" + (($source.Substring($project.Length + 1) -replace '[\\/]','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$source,"/Fo$object","/Fd$out/transport.pdb")
        $transportObjects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/transport.exe") +
        $transportObjects + @('/link','/incremental:no','/DEBUG','/WX'))
    foreach ($mode in @('solo','ok','pending','wrap','busy','clock','stalled','read','high','low','timeout')) {
        Invoke-Checked "$out/transport.exe" @($mode)
    }
    $outputSources = @('arch/x86/kernel/cpu/percpu/panic/output/write.c','test/arch/x86/kernel/cpu/percpu/panic/output.c') |
        ForEach-Object { Join-Path $project $_ }
    $outputObjects = @("$out/coordinator.obj", "$out/arch-x86-kernel-cpu-percpu-panic-records.obj")
    foreach ($source in $outputSources) {
        $object = "$out/" + (($source.Substring($project.Length + 1) -replace '[\\/]','-') -replace '\.c$','.obj')
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/fsanitize=address',
            '/MD','/O2','/UNDEBUG','/c',"/I$project",$source,"/Fo$object","/Fd$out/output.pdb")
        $outputObjects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/output.exe") +
        $outputObjects + @('/link','/incremental:no','/DEBUG','/WX'))
    foreach ($mode in @('ok','wrap','absent','dlab','busy','write','bytes','deadline','clock-start','clock-mid')) {
        Invoke-Checked "$out/output.exe" @($mode)
    }
    Invoke-Cppcheck $project $out ($sources + $transportSources + $outputSources)
} finally { Restore-Msvc $saved }
"PASS: per-CPU panic protocol host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
