param([Parameter(Mandatory)][string]$ToolchainBin, [string]$OutputDirectory,
      [ValidateSet('none', 'contract-write', 'stack-guard', 'execute-data')][string]$Fault = 'none')
$ErrorActionPreference = 'Stop'
$bootRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = (Resolve-Path (Join-Path $bootRoot '..\..\..\..')).Path
$output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $bootRoot 'build' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
. (Join-Path $PSScriptRoot 'status.ps1')

# ====== compilazione del solo kernel diagnostico ====== #

Write-BootSection 'kernel C diagnostico'
[string[]]$probeFlags = @()
if ($Fault -ne 'none') { $probeFlags = @('/DBOOT_PROBE_FAULT=' + (@('none','contract-write','stack-guard','execute-data').IndexOf($Fault))) }
& (Join-Path $ToolchainBin 'cl.exe') /nologo /TC /std:c11 /W4 /WX /GS- /Zl /O1 /c `
    @probeFlags ('/I' + $bootRoot) ('/I' + $projectRoot) (Join-Path $bootRoot 'test\kernel\entry.c') ("/Fo$output\kernel_probe.obj")
if ($LASTEXITCODE -ne 0) { throw 'Compilazione kernel diagnostico fallita.' }
& (Join-Path $ToolchainBin 'link.exe') /nologo /machine:x64 /subsystem:native `
    /entry:kernel_probe_entry /nodefaultlib /incremental:no /dynamicbase:no /fixed /base:0x100000000 `
    ("/out:$output\kernel_probe.exe") ("$output\kernel_probe.obj")
if ($LASTEXITCODE -ne 0) { throw 'Collegamento kernel diagnostico fallito.' }

# ====== lettura limitata del PE prodotto localmente ====== #

$bytes = [IO.File]::ReadAllBytes("$output\kernel_probe.exe")
function Read-Number([int]$Offset, [int]$Width) {
    if ($Offset -lt 0 -or $Offset -gt $bytes.Length - $Width) { throw 'PE troncato.' }
    switch ($Width) {
        2 { return [BitConverter]::ToUInt16($bytes, $Offset) }
        4 { return [BitConverter]::ToUInt32($bytes, $Offset) }
        8 { return [BitConverter]::ToUInt64($bytes, $Offset) }
        default { throw 'Larghezza non supportata.' }
    }
}
$pe = Read-Number 60 4
if ($pe -gt 65536 -or (Read-Number 0 2) -ne 0x5a4d -or
    (Read-Number $pe 4) -ne 0x4550 -or (Read-Number ($pe + 4) 2) -ne 0x8664) {
    throw 'Formato PE x64 inatteso.'
}
$optional = $pe + 24
$optionalSize = Read-Number ($pe + 20) 2
$count = Read-Number ($pe + 6) 2
$expectedCount = if ($Fault -eq 'execute-data') { 4 } else { 3 }
if ($count -ne $expectedCount -or $optionalSize -ne 240 -or
    (Read-Number $optional 2) -ne 0x20b -or
    (Read-Number ($optional + 24) 8) -ne 0x100000000 -or
    (Read-Number ($optional + 32) 4) -ne 4096 -or
    (Read-Number ($optional + 108) 4) -ne 16) { throw 'Profilo PE diagnostico inatteso.' }
for ($i = 0; $i -lt 16; $i++) {
    if ($i -ne 6 -and !($Fault -eq 'execute-data' -and $i -eq 3) -and
        (Read-Number ($optional + 112 + $i * 8) 8) -ne 0) {
        throw 'Il probe deve essere privo di import, relocation, TLS e directory runtime.'
    }
}
$entry = [uint64]0x100000000 + (Read-Number ($optional + 16) 4)
$sections = @()
$previousEnd = [uint64]0x100000000
$entryValid = $false
$payloadEnd = 4096
for ($i = 0; $i -lt $count; $i++) {
    $header = $optional + $optionalSize + $i * 40
    $null = Read-Number ($header + 36) 4
    $name = [Text.Encoding]::ASCII.GetString($bytes, $header, 8).TrimEnd([char]0)
    $virtualSize = Read-Number ($header + 8) 4
    $address = [uint64]0x100000000 + (Read-Number ($header + 12) 4)
    $rawSize = Read-Number ($header + 16) 4
    $rawOffset = Read-Number ($header + 20) 4
    $characteristics = Read-Number ($header + 36) 4
    $expectedName = if ($i -eq 0) { '.text' } elseif ($i -eq 1) { '.rdata' } elseif ($i -eq 2) { '.data' } else { '.pdata' }
    $expectedPermissions = if ($i -eq 0) { [uint32]0x60000000 } elseif ($i -eq 2) { [uint32]3221225472 } else { [uint32]0x40000000 }
    if ($name -ne $expectedName -or ($characteristics -band [uint32]3758096384) -ne $expectedPermissions -or
        $virtualSize -eq 0 -or $virtualSize -gt 4096 -or $rawSize -gt 4096 -or
        $rawOffset -gt $bytes.Length -or $rawSize -gt $bytes.Length - $rawOffset -or
        ($address -band 4095) -ne 0 -or $address -lt $previousEnd -or $address -ge 0x100100000) {
        throw 'Sezione fuori dal profilo ristretto del probe.'
    }
    if ($i -eq 0 -and $rawSize -eq 0) { throw 'Codice assente.' }
    if ($i -eq 2 -and $rawSize -ne 0) { throw 'Il marker deve essere BSS azzerata.' }
    $memorySize = [Math]::Max($virtualSize, $rawSize)
    $fileOffset = 4096 * ($i + 1)
    $flags = if ($i -eq 0) { 5 } elseif ($i -eq 2) { 6 } else { 4 }
    $sections += @{ Address=$address; Offset=$fileOffset; RawOffset=$rawOffset;
        FileBytes=$rawSize; MemoryBytes=$memorySize; Flags=$flags }
    if ($i -eq 1) {
        $debugRva = Read-Number ($optional + 160) 4
        $debugBytes = Read-Number ($optional + 164) 4
        $sectionRva = $address - 0x100000000
        if ($debugBytes -ne 0 -and ($debugRva -lt $sectionRva -or
            $debugRva + [uint64]$debugBytes -gt $sectionRva + $virtualSize)) {
            throw 'Directory debug fuori dai dati in sola lettura.'
        }
    }
    if ($i -eq 3) {
        $exceptionRva = Read-Number ($optional + 136) 4
        $exceptionBytes = Read-Number ($optional + 140) 4
        $sectionRva = $address - 0x100000000
        if (!$exceptionBytes -or $exceptionRva -lt $sectionRva -or
            $exceptionRva + [uint64]$exceptionBytes -gt $sectionRva + $virtualSize) {
            throw 'Directory unwind del solo probe NX fuori da .pdata.'
        }
    }
    $previousEnd = $address + 4096
    $payloadEnd = [Math]::Max($payloadEnd, $fileOffset + $rawSize)
    if ($i -eq 0 -and $entry -ge $address -and $entry -lt $address + $rawSize) { $entryValid = $true }
}
if (!$entryValid) { throw 'Ingresso non contenuto nel codice.' }

# ====== ELF64 con indirizzi e byte macchina conservati ====== #

$elf = New-Object byte[] $payloadEnd
function Put-Number([int]$Offset, [uint64]$Value, [int]$Width) {
    $encoded = [BitConverter]::GetBytes($Value)
    [Array]::Copy($encoded, 0, $elf, $Offset, $Width)
}
$elf[0]=0x7f; $elf[1]=0x45; $elf[2]=0x4c; $elf[3]=0x46
$elf[4]=2; $elf[5]=1; $elf[6]=1
Put-Number 16 2 2
Put-Number 18 62 2
Put-Number 20 1 4
Put-Number 24 $entry 8
Put-Number 32 64 8
Put-Number 52 64 2
Put-Number 54 56 2
Put-Number 56 $count 2
for ($i = 0; $i -lt $count; $i++) {
    $segment = $sections[$i]
    $header = 64 + $i * 56
    Put-Number $header 1 4
    Put-Number ($header + 4) $segment.Flags 4
    Put-Number ($header + 8) $segment.Offset 8
    Put-Number ($header + 16) $segment.Address 8
    Put-Number ($header + 24) $segment.Address 8
    Put-Number ($header + 32) $segment.FileBytes 8
    Put-Number ($header + 40) $segment.MemoryBytes 8
    Put-Number ($header + 48) 4096 8
    [Array]::Copy($bytes, $segment.RawOffset, $elf, $segment.Offset, $segment.FileBytes)
}
[IO.File]::WriteAllBytes("$output\kernel_probe.elf", $elf)
Write-BootStatus OK "Probe ELF generato: $($elf.Length) byte, codice RX e marker BSS RW."
