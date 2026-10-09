param([ValidateRange(1,20)][int]$Boots = 10, [string]$Python,
    [string]$KernelDirectory, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../../../..')).Path
if (!$KernelDirectory) { $KernelDirectory = Join-Path $project 'kernel/build/ps2-input-native' }
if (!$Python) {
    $Python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
}
if (!(Test-Path -LiteralPath $Python)) { throw 'Provide -Python with a Python 3 executable.' }

# ============ isolated input profile ============

if (!$SkipBuild) {
    & "$project/kernel/scripts/build.ps1" -Ps2Input -OutputDirectory $KernelDirectory
    if (!$?) { throw 'Input profile build failed' }
}

# ============ owned VM keyboard injection and final cleanup ============

& $Python "$PSScriptRoot/firmware.py" --image "$KernelDirectory/kernel.elf" --boots $Boots --text --shell
if ($LASTEXITCODE) { throw 'Input firmware verification failed' }
