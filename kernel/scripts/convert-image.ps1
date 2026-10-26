param([Parameter(Mandatory)][string]$Image, [Parameter(Mandatory)][string]$Output)
. (Join-Path $PSScriptRoot '../../scripts/lib/layout.ps1')
$ErrorActionPreference = 'Stop'

# ====== PE locale limitato senza import TLS o rilocazioni ====== #

$data = [IO.File]::ReadAllBytes($Image)
if ($data.Length -lt 256 -or $data.Length -gt 16777216) { throw 'Dimensione PE fuori profilo.' }
function Read-Field([int]$offset, [int]$width) {
    if ($offset -lt 0 -or $offset -gt $data.Length - $width) { throw 'PE troncato.' }
    if ($width -eq 2) { return [BitConverter]::ToUInt16($data, $offset) }
    if ($width -eq 4) { return [BitConverter]::ToUInt32($data, $offset) }
    if ($width -eq 8) { return [BitConverter]::ToUInt64($data, $offset) }
    throw 'Larghezza non supportata.'
}
$pe = Read-Field 60 4
if ($pe -gt 65536 -or (Read-Field 0 2) -ne 0x5a4d -or (Read-Field $pe 4) -ne 0x4550 -or
    (Read-Field ($pe + 4) 2) -ne 0x8664) { throw 'PE x64 richiesto.' }
$optional = $pe + 24
$count = Read-Field ($pe + 6) 2
if ($count -lt 1 -or $count -gt 16 -or (Read-Field ($pe + 20) 2) -ne 240 -or
    (Read-Field $optional 2) -ne 0x20b -or (Read-Field ($optional + 24) 8) -ne $OsImageBase -or
    (Read-Field ($optional + 32) 4) -ne 4096 -or (Read-Field ($optional + 108) 4) -ne 16) {
    throw 'Profilo kernel PE inatteso.'
}
for ($index = 0; $index -lt 16; ++$index) {
    if ($index -notin @(3,6) -and (Read-Field ($optional + 112 + 8*$index) 8) -ne 0) {
        throw 'Directory runtime non consentita.'
    }
}
$entry = $OsImageBase + (Read-Field ($optional + 16) 4)
$sections = @()
$fileEnd = 4096
$previousEnd = $OsImageBase
$entryFound = $false
$names = @{}

# ====== sezioni con permessi W xor X e limiti indipendenti ====== #

for ($index = 0; $index -lt $count; ++$index) {
    $header = $optional + 240 + $index*40
    $null = Read-Field ($header + 36) 4
    $name = [Text.Encoding]::ASCII.GetString($data, $header, 8).TrimEnd([char]0)
    if ($names.ContainsKey($name)) { throw 'Sezione duplicata.' }
    $names[$name] = $true
    $size = Read-Field ($header + 8) 4
    $rva = Read-Field ($header + 12) 4
    $rawSize = Read-Field ($header + 16) 4
    $raw = Read-Field ($header + 20) 4
    $permissions = (Read-Field ($header + 36) 4) -band [uint32]3758096384
    $expected = switch ($name) {
        '.text' { [uint32]1610612736 }
        '.data' { [uint32]3221225472 }
        '.rdata' { [uint32]1073741824 }
        '.pdata' { [uint32]1073741824 }
        default { throw "Sezione non supportata: $name" }
    }
    $address = $OsImageBase + $rva
    $memorySize = [Math]::Max($size, $rawSize)
    if ($permissions -ne $expected -or !$memorySize -or $memorySize -gt 134217728 -or
        ($address -band 4095) -ne 0 -or $address -lt $previousEnd -or
        $address + $memorySize -gt $OsImageBase + 0x8000000 -or $raw -gt $data.Length -or $rawSize -gt $data.Length - $raw) {
        throw 'Sezione fuori limiti o con permessi inattesi.'
    }
    $flags = if ($name -eq '.text') { 5 } elseif ($name -eq '.data') { 6 } else { 4 }
    foreach ($directory in @(3,6)) {
        $directoryRva = Read-Field ($optional + 112 + 8*$directory) 4
        $directorySize = Read-Field ($optional + 116 + 8*$directory) 4
        if ($directorySize -and $directoryRva -ge $rva -and $directoryRva -lt $rva + $memorySize) {
            if ($flags -ne 4 -or $directoryRva + [uint64]$directorySize -gt $rva + $memorySize) {
                throw 'Metadati PE fuori dalla sezione in sola lettura.'
            }
        }
    }
    $sections += @{ Address=$address; Offset=$fileEnd; Raw=$raw; Bytes=$rawSize; Memory=$memorySize; Flags=$flags }
    if ($flags -eq 5 -and $entry -ge $address -and $entry -lt $address + $rawSize) { $entryFound = $true }
    $previousEnd = $address + [uint64]([Math]::Ceiling($memorySize / 4096.0)*4096)
    $fileEnd += [int]([Math]::Ceiling($rawSize / 4096.0)*4096)
}
if (!$entryFound) { throw 'Ingresso fuori dal codice.' }
foreach ($directory in @(3,6)) {
    $rva = Read-Field ($optional + 112 + 8*$directory) 4
    $size = Read-Field ($optional + 116 + 8*$directory) 4
    if (!$rva -and !$size) { continue }
    $contained = $false
    foreach ($section in $sections) {
        $start = $section.Address - $OsImageBase
        if ($rva -and $size -and $section.Flags -eq 4 -and $rva -ge $start -and
            $rva + [uint64]$size -le $start + $section.Bytes) { $contained = $true }
    }
    if (!$contained) { throw 'Directory PE non contenuta nei dati presenti in sola lettura.' }
}

# ====== ELF64 statico conserva indirizzi macchina e BSS ====== #

$elf = [byte[]]::new($fileEnd)
function Put-Field([int]$offset, [uint64]$value, [int]$width) {
    [Array]::Copy([BitConverter]::GetBytes($value), 0, $elf, $offset, $width)
}
$elf[0]=127; $elf[1]=69; $elf[2]=76; $elf[3]=70; $elf[4]=2; $elf[5]=1; $elf[6]=1
Put-Field 16 2 2; Put-Field 18 62 2; Put-Field 20 1 4
Put-Field 24 $entry 8; Put-Field 32 64 8
Put-Field 52 64 2; Put-Field 54 56 2; Put-Field 56 $count 2
for ($index = 0; $index -lt $count; ++$index) {
    $section = $sections[$index]
    $header = 64 + $index*56
    Put-Field $header 1 4; Put-Field ($header + 4) $section.Flags 4
    Put-Field ($header + 8) $section.Offset 8
    Put-Field ($header + 16) $section.Address 8; Put-Field ($header + 24) $section.Address 8
    Put-Field ($header + 32) $section.Bytes 8; Put-Field ($header + 40) $section.Memory 8
    Put-Field ($header + 48) 4096 8
    [Array]::Copy($data, $section.Raw, $elf, $section.Offset, $section.Bytes)
}
[IO.File]::WriteAllBytes($Output, $elf)
