param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-wake-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ============ real protocol, transport and host sanitizer ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Invoke-OsChecked $Program $Arguments -Log "$out/tools.log" -Tee
}
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2',"$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",
    '-o',"$out/protocol.obj")
$sources = @('arch/x86/kernel/cpu/percpu/startup/transport/wake.c','arch/x86/kernel/interrupts/apic/icr/icr.c','kernel/time/clock.c',
    'test/arch/x86/kernel/cpu/percpu/startup/transport/wake.c')
$saved = Use-Msvc
try {
    $objects = @("$out/protocol.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/O2',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source",
            "/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    $cases = @('fast','second','wrap','wrap32','busy','clock','stalled','backward','guard',
        'esr-before','esr-after','never','ready-io','ready-invalid','clock-10','guard-10','state-io') +
        @(1..8 | ForEach-Object { "read-$_" }) + @(1..8 | ForEach-Object { "write-$_" }) +
        @(9..12 | ForEach-Object { "second-read-$_" }) + @(9..12 | ForEach-Object { "second-write-$_" })
    foreach ($case in $cases) { Invoke-Checked "$out/host.exe" @($case) }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
[ordered]@{ Status = 'HOST_WAKE_PASSED'; Cases = $cases.Count; ModuleReady = $false;
    GuestBoots = 0; NativeMmio = $false; PhysicalAccounting = 'N/A'; CAsan = $true } |
    ConvertTo-Json | Set-Content -LiteralPath "$out/results.json" -Encoding UTF8
"PASS: wake transport host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
