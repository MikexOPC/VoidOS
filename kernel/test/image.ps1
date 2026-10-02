$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'arch/x86/boot/uefi/scripts/status.ps1')
$output = Join-Path $project ('kernel/build/image-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = [IO.File]::ReadAllBytes((Join-Path $project 'kernel/build/kernel.exe'))
$pe = [BitConverter]::ToUInt32($source, 60)
$optional = $pe + 24
$section = $optional + 240
$converter = Join-Path $project 'kernel/scripts/convert-image.ps1'
Write-BootSection 'limiti conversione immagine kernel'

# ====== mutazioni isolate non eseguite e diagnostica specifica attesa ====== #

$cases = @(
    @{Name='import';Offset=($optional+120);Value=1;Width=8;Reason='Directory runtime'},
    @{Name='writable-code';Offset=($section+36);Value=3758096416;Width=4;Reason='Sezione fuori limiti'},
    @{Name='raw-outside';Offset=($section+20);Value=4294967295;Width=4;Reason='Sezione fuori limiti'},
    @{Name='entry-outside';Offset=($optional+16);Value=0;Width=4;Reason='Ingresso fuori'},
    @{Name='directory-outside';Offset=($optional+136);Value=4294967297;Width=8;Reason='Directory PE non contenuta'},
    @{Name='overflow-memory';Offset=($section+8);Value=4294967295;Width=4;Reason='Sezione fuori limiti'}
)
foreach ($case in $cases) {
    $bytes = [byte[]]$source.Clone()
    [Array]::Copy([BitConverter]::GetBytes([uint64]$case.Value), 0, $bytes, $case.Offset, $case.Width)
    $image = Join-Path $output ($case.Name + '.exe')
    [IO.File]::WriteAllBytes($image, $bytes)
    $failed = $false
    try { & $converter -Image $image -Output (Join-Path $output ($case.Name + '.elf')) }
    catch {
        if ($_.Exception.Message -notmatch $case.Reason) { throw }
        $failed = $true
    }
    if (!$failed) { throw "Immagine invalida accettata: $($case.Name)" }
}
$truncated = Join-Path $output 'truncated.exe'
[IO.File]::WriteAllBytes($truncated, $source[0..127])
$failed = $false
try { & $converter -Image $truncated -Output (Join-Path $output 'truncated.elf') }
catch { if ($_.Exception.Message -notmatch 'Dimensione PE') { throw }; $failed = $true }
if (!$failed) { throw 'PE troncato accettato.' }
& $converter -Image (Join-Path $project 'kernel/build/kernel.exe') -Output (Join-Path $output 'valid.elf')
Write-BootStatus OK 'Immagine valida e 7 mutazioni invalide verificate.'
