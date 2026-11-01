param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$tool = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-blob-host-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ============ actual flat blob and safe layout checks ============

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Invoke-OsChecked $Program $Arguments -Log "$out/tools.log" -Tee
}
Invoke-Checked $tool.Nasm @('-f','bin','-Wall','-Werror','-l',"$out/blob.lst",'-o',"$out/blob.bin",
    "$project/arch/x86/kernel/cpu/percpu/startup/assembly/trampoline/entry.asm")
$bytes = [IO.File]::ReadAllBytes("$out/blob.bin")
if ($bytes.Length -ne 4096) { throw 'Blob must occupy exactly one page' }
$entry32 = [BitConverter]::ToUInt16($bytes, 16)
$entry64 = [BitConverter]::ToUInt16($bytes, 18)
$gdt = [BitConverter]::ToUInt16($bytes, 20)
foreach ($part in @(@{ Bits = 16; Start = 32; End = $entry32 },
    @{ Bits = 32; Start = $entry32; End = $entry64 }, @{ Bits = 64; Start = $entry64; End = $gdt })) {
    $slice = New-Object byte[] ($part.End - $part.Start)
    [Array]::Copy($bytes, $part.Start, $slice, 0, $slice.Length)
    [IO.File]::WriteAllBytes("$out/mode-$($part.Bits).bin", $slice)
    Invoke-OsChecked $tool.Ndisasm @('-b',"$($part.Bits)","$out/mode-$($part.Bits).bin") -Log "$out/decode-$($part.Bits).log"
}
foreach ($mode in @('debug','release')) {
    $flags = @('--edition=2024','--test','-D','warnings',
        "$project/test/arch/x86/kernel/cpu/percpu/startup/rust/blob/cases.rs",'-o',"$out/$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C','opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/$mode.exe" @()
}
Invoke-Checked rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','no-vectorize-loops','-C','no-vectorize-slp',
    "$project/arch/x86/kernel/cpu/percpu/startup/rust/protocol/api.rs",'-o',"$out/protocol.obj")

# ============ C ownership and byte-for-byte package verification ============

$sources = @('arch/x86/kernel/cpu/percpu/startup/blob/package.c',
    'arch/x86/kernel/cpu/percpu/startup/entry.c','test/arch/x86/kernel/cpu/percpu/startup/blob/package.c')
$saved = Use-Msvc
try {
    $objects = @("$out/protocol.obj")
    foreach ($source in $sources) {
        $object = "$out/" + ($source -replace '/','-') + '.obj'
        Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/Zi','/O2',
            '/fsanitize=address','/MD','/UNDEBUG',"/I$project",'/c',"$project/$source","/Fo$object","/Fd$out/host.pdb")
        $objects += $object
    }
    Invoke-Checked "$($saved.Bin)/cl.exe" (@('/nologo','/MD','/fsanitize=address',"/Fe$out/host.exe") +
        $objects + @('/link','/incremental:no','/DEBUG','/WX'))
    Invoke-Checked "$out/host.exe" @("$out/blob.bin","$out/package")
    Invoke-Checked "$($saved.Bin)/cl.exe" @('/nologo','/TC','/std:c11','/W4','/WX','/O2','/GS-','/Zl',
        "/I$project",'/c',"$project/arch/x86/kernel/cpu/percpu/startup/blob/package.c","/Fo$out/freestanding.obj")
    Invoke-OsChecked "$($saved.Bin)/dumpbin.exe" @('/symbols',"$out/freestanding.obj") -Log "$out/freestanding-symbols.log"
    $unresolved = @(Get-Content "$out/freestanding-symbols.log" | Where-Object { $_ -match '\bUNDEF\b' })
    if (@($unresolved | Where-Object { $_ -notmatch '\| xs_(entry_valid|blob_layout)$' }).Count) {
        throw 'Unexpected freestanding runtime dependency'
    }
    Invoke-Cppcheck $project $out @($sources | ForEach-Object { "$project/$_" })
} finally { Restore-Msvc $saved }
[ordered]@{ Status = 'HOST_BLOB_PASSED'; ModuleReady = $false; ApStarted = 0;
    KernelInstalled = $false; CAsan = $true; BlobHash = (Get-FileHash "$out/blob.bin").Hash } |
    ConvertTo-Json | Set-Content "$out/result.json" -Encoding UTF8
"PASS: blob host evidence $out"
if ($MyInvocation.InvocationName -ne '.') { exit 0 }
