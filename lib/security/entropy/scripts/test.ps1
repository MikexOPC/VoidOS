param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
    Join-Path $project 'kernel/build/security-entropy-host'
}
New-Item -ItemType Directory -Force $out | Out-Null
$source = "$project/lib/security/entropy/rust/health.rs"
$test = "$project/test/lib/security/entropy/rust/health_test.rs"

# ============ host tests debug and release ============

foreach ($mode in @('debug', 'release')) {
    $flags = @('--edition=2024', '--test', '-D', 'warnings', '-C', 'overflow-checks=yes', $test, '-o', "$out/health-$mode.exe")
    if ($mode -eq 'release') { $flags += @('-C', 'opt-level=2') }
    Invoke-Checked rustc $flags
    Invoke-Checked "$out/health-$mode.exe" @()
}

# ============ clippy and freestanding object ============

Invoke-Checked clippy-driver @('--edition=2024', '--test', '-D', 'warnings', '-D', 'clippy::all', '--emit=metadata', $test, '-o', "$out/tests.rmeta")
Invoke-Checked clippy-driver @('--edition=2024', '--crate-type=lib', '-D', 'warnings', '-D', 'clippy::all', '--emit=metadata', $source, '-o', "$out/health.rmeta")
Invoke-Checked rustc @('--edition=2024', '--crate-type=lib', '--emit=obj', '--target', 'x86_64-pc-windows-msvc',
    '-C', 'panic=abort', '-C', 'opt-level=2', '-D', 'warnings', $source, '-o', "$out/health.obj")
"PASS: security entropy host evidence $out"
