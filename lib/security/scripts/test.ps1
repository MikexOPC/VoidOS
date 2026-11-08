param([string]$Module)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$out = Join-Path $project ('kernel/build/security-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Write-Host "[ INFO ] Security host evidence: $out"
rustc --edition=2024 -D warnings -C opt-level=2 "$project/test/lib/security/runner.rs" -o "$out/runner.exe"
if ($LASTEXITCODE) { throw 'Security runner build failed' }
& "$out/runner.exe" $project $out @(@($Module) | Where-Object { $_ })
if ($LASTEXITCODE) { throw 'Security host suite failed' }
