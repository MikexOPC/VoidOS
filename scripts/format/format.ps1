# Project formatter: clang-format (.clang-format) then the conditions/blank-line pass.
# Generated sources and reuse/ are excluded: their generators or history own the layout.
param([string[]]$Paths)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location $project
$format = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-format.exe' -ErrorAction Stop
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$excluded = @('arch/x86/kernel/drivers/video/framebuffer/font/font.c', 'kernel/console/video/pointer/shape.c')
if (!$Paths) { $Paths = @(git ls-files '*.c' '*.h' | Where-Object { $_ -notlike 'reuse/*' }) }
$files = @($Paths | ForEach-Object { $_.Replace('\', '/') } | Where-Object { $excluded -notcontains $_ })
for ($index = 0; $index -lt $files.Count; $index += 40) {
    $batch = $files[$index..([Math]::Min($index + 39, $files.Count - 1))]
    & $format.FullName -i @batch
    if ($LASTEXITCODE) { throw 'clang-format failed' }
    & $python -I -B (Join-Path $PSScriptRoot 'conditions.py') @batch
    if ($LASTEXITCODE) { throw 'conditions.py failed' }
}
"Formatted $($files.Count) files"
