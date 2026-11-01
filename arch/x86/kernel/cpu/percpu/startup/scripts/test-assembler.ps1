param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
$tool = & (Join-Path $PSScriptRoot 'assembler.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project ('kernel/build/ap-assembler-' + [guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
. (Join-Path $project 'scripts/lib/common.ps1')
$source = Join-Path $project 'test/arch/x86/kernel/cpu/percpu/startup/assembly/toolchain/modes.asm'

# ============ flat binary mode selection and reproducibility ============

Invoke-OsChecked $tool.Nasm @('-v') -Log "$out/tools.log"
foreach ($name in @('first','second')) {
    Invoke-OsChecked $tool.Nasm @('-f','bin','-Wall','-Werror','-o',"$out/$name.bin",$source) -Log "$out/tools.log"
}
$hex = -join ([IO.File]::ReadAllBytes("$out/first.bin") | ForEach-Object { $_.ToString('X2') })
if ($hex -ne 'B83412B87856341248B88877665544332211') { throw 'Unexpected mixed-mode encoding' }
if ((Get-FileHash "$out/first.bin").Hash -ne (Get-FileHash "$out/second.bin").Hash) {
    throw 'Non-deterministic flat binary'
}

# ============ decode the three regions in their actual assembly modes ============

foreach ($case in @(@{ Bits = 16; Start = 0; Bytes = 3; Text = 'mov ax,0x1234' },
    @{ Bits = 32; Start = 3; Bytes = 5; Text = 'mov eax,0x12345678' },
    @{ Bits = 64; Start = 8; Bytes = 10; Text = 'mov rax,0x1122334455667788' })) {
    $part = New-Object byte[] $case.Bytes
    [Array]::Copy([IO.File]::ReadAllBytes("$out/first.bin"), $case.Start, $part, 0, $case.Bytes)
    $path = "$out/mode-$($case.Bits).bin"
    [IO.File]::WriteAllBytes($path, $part)
    Invoke-OsChecked $tool.Ndisasm @('-b',"$($case.Bits)",$path) -Log "$out/decode-$($case.Bits).log"
    if (!(Select-String -LiteralPath "$out/decode-$($case.Bits).log" -SimpleMatch $case.Text)) {
        throw 'Wrong decoded instruction'
    }
}
[ordered]@{ Status = 'ASSEMBLER_SMOKE_PASSED'; ModuleReady = $false; GuestBoots = 0
    Nasm = $tool.Version; FixtureBytes = 18; SourceHash = (Get-FileHash $source).Hash
    BinaryHash = (Get-FileHash "$out/first.bin").Hash } |
    ConvertTo-Json | Set-Content -LiteralPath "$out/result.json" -Encoding UTF8
"PASS: assembler-only evidence $out; no CPU mode transition executed"
