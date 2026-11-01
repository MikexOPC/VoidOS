param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-startup-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $out | Out-Null

# ============ persistent tool evidence ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Invoke-OsChecked $Program $Arguments -Log "$out/tools.log" -Tee
}

# ============ Rust protocol tests ============

foreach ($mode in @('debug','release')) {
    foreach ($suite in @('protocol','record')) {
        $flags = @('--edition=2024','--test','-D','warnings',
            "$project/test/arch/x86/kernel/cpu/percpu/startup/rust/$suite/cases.rs",'-o',"$out/$suite-$mode.exe")
        if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
        Invoke-Checked rustc $flags
        Invoke-Checked "$out/$suite-$mode.exe" @()
    }
}
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings',
    '-C','panic=abort','-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
    "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',"$out/protocol.obj")

# ============ C ABI and host sanitizer ============

$saved = Use-Msvc
try {
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
        '/fsanitize=address','/MD','/O2','/UNDEBUG',"/I$project",
        '/c',"$project/test/arch/x86/kernel/cpu/percpu/startup/ffi.c",
        "/Fo$out/ffi.obj","/Fd$out/ffi.pdb")
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/MD','/fsanitize=address',
        "$out/ffi.obj","$out/protocol.obj","/Fe$out/ffi.exe",'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/ffi.exe" @()
    foreach ($source in @('arch/x86/kernel/cpu/percpu/startup/entry.c',
        'test/arch/x86/kernel/cpu/percpu/startup/record.c')) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi',
            '/fsanitize=address','/MD','/O2','/UNDEBUG',"/I$project",'/c',"$project/$source",
            "/Fo$object","/Fd$out/record.pdb")
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/MD','/fsanitize=address',
        "$out/arch-x86-kernel-cpu-percpu-startup-entry.c.obj",
        "$out/test-arch-x86-kernel-cpu-percpu-startup-record.c.obj","$out/protocol.obj",
        "/Fe$out/record.exe",'/link','/incremental:no','/DEBUG','/WX')
    Invoke-Checked "$out/record.exe" @()
    Invoke-Cppcheck $project $out @("$project/arch/x86/kernel/cpu/percpu/startup/entry.c",
        "$project/test/arch/x86/kernel/cpu/percpu/startup/record.c")
} finally { Restore-Msvc $saved }
[ordered]@{
    Status = 'HOST_PROTOCOL_RECORD_PASSED'; ModuleReady = $false; GuestBoots = 0
    RustSuites = @('protocol','record'); CExecutables = @('ffi','record'); Cppcheck = 'PASSED'
    RustModes = @('debug','release'); CAsan = $true; RustAsan = $false
    RuntimeIntegrated = $false; Coverage = 'N/D'; Timestamp = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath "$out/result.json" -Encoding UTF8
"PASS: AP startup host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
