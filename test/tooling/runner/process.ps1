param()
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
. (Join-Path $project 'scripts/test/run.ps1') -List | Out-Null
$runtime = Resolve-SuiteShell
$savedPath = $env:PATH
try {
    $env:PATH = $env:SystemRoot + '/System32'
    if (!(Test-Path -LiteralPath (Resolve-SuiteShell) -PathType Leaf)) {
        throw 'PowerShell 7 fallback unavailable'
    }
} finally {
    $env:PATH = $savedPath
}
$temporary = Join-Path $project ('kernel/build/runner-regression-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null

# ============ stderr is data; nonzero exit remains failure ============

foreach ($code in @(0,7)) {
    $script = Join-Path $temporary ("child with spaces $code.ps1")
    [IO.File]::WriteAllText($script, '[Console]::Out.WriteLine("stdout-marker"); ' +
        '[Console]::Error.WriteLine("stderr-marker"); exit ' + $code)
    $log = Join-Path $temporary ("out-$code.log")
    $errorLog = Join-Path $temporary ("error-$code.log")
    $actual = Invoke-IsolatedSuite $runtime $script $log $errorLog
    if ($actual -ne $code -or (Get-Content $log -Raw).Trim() -ne 'stdout-marker' -or
        (Get-Content $errorLog -Raw).Trim() -ne 'stderr-marker') {
        throw "Child accounting failure: expected $code, actual $actual"
    }
}
Write-Output 'PASS: stderr preserved, zero exit accepted, nonzero exit retained, paths with spaces'

# ============ boot reporting ============

$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $python -B -m unittest discover -s (Join-Path $PSScriptRoot '.') -p '*_test.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Boot report regression failed' }
& (Join-Path $PSScriptRoot 'report.ps1')
