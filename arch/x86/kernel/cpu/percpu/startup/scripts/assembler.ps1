param([switch]$Download)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
$tools = Join-Path $project 'kernel/build/tools/nasm-3.01'
$directory = Join-Path $tools 'package/nasm-3.01'
$nasm = Join-Path $directory 'nasm.exe'
$ndisasm = Join-Path $directory 'ndisasm.exe'
$archiveHash = 'E0BA5157007ABC7B1A65118A96657A961DDF55F7E3F632EE035366DFCE039CA4'

# ============ explicit portable dependency setup ============

if ($Download) {
    New-Item -ItemType Directory -Force -Path $tools | Out-Null
    $archive = Join-Path $tools 'package.zip'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://www.nasm.us/pub/nasm/releasebuilds/3.01/win64/nasm-3.01-win64.zip' -OutFile $archive
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $archiveHash) {
        throw 'NASM archive differs from the pinned package; extraction refused.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $tools 'package') -Force
}

# ============ verify exact executables before use ============

foreach ($item in @(
    @{ Path = $nasm; Hash = '5F59C792CC3B92316A4E6D61CAE192588A0D8F8FA30B33988A0F2230E5A9C22C' },
    @{ Path = $ndisasm; Hash = '5CFA27135C011DAB4663694E15808DDB80A96BE23C1052B4B9B80C9DA6EF059A' })) {
    if (!(Test-Path -LiteralPath $item.Path) -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $item.Path).Hash -ne $item.Hash) {
        throw 'Pinned NASM tools absent or changed. Run assembler.ps1 -Download explicitly.'
    }
}
[pscustomobject]@{ Nasm = $nasm; Ndisasm = $ndisasm; Version = '3.01'; ArchiveHash = $archiveHash }
