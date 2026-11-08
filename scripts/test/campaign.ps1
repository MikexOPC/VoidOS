param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][int]$Count,
    [Parameter(Mandatory)][string]$Output
)
# One campaign of campaigns.json: build its image into $Output/$Name, run the runner, exit with its
# code. Called by qemu.ps1, sequentially or several at once (each campaign has its own directories).
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use qemu.ps1: campaign builds require PowerShell 7.' }
$campaignShell = (Get-Process -Id $PID).Path
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$entry = (Get-Content -Raw (Join-Path $PSScriptRoot 'campaigns.json') | ConvertFrom-Json).campaigns.$Name
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$log = Join-Path $Output ($Name + '.log')
$image = ''
$code = 0

# ============ build and run ============

try {
    if ($null -ne $entry.build) {
        $images = Join-Path $Output $Name
        $build = @{ OutputDirectory = $images }
        for ($index = 0; $index -lt $entry.build.Count; ++$index) {
            $token = [string]$entry.build[$index]
            if ($token -notmatch '^-[A-Za-z][A-Za-z0-9]*$') { throw "Invalid build parameter: $token" }
            $key = $token.Substring(1)
            if ($build.ContainsKey($key)) { throw "Duplicate build parameter: $key" }
            $value = $true
            if ($index + 1 -lt $entry.build.Count -and !([string]$entry.build[$index + 1]).StartsWith('-')) {
                $value = $entry.build[++$index]
            }
            $build[$key] = $value
        }
        & (Join-Path $project 'kernel/scripts/build.ps1') @build *> (Join-Path $Output ($Name + '.build.log'))
        if ($LASTEXITCODE) { throw "Build failed: $Name" }
        $image = Join-Path $images 'kernel.elf'
    }
    $arguments = @($entry.runner | Select-Object -Skip 1 | ForEach-Object {
        $_.Replace('{image}', $image).Replace('{boots}', [string]$Count) })
    if ($entry.runner[0] -eq 'qemu-runner') {
        $meson = (Get-Command meson -ErrorAction SilentlyContinue).Source
        if (!$meson) { $meson = Join-Path $env:APPDATA 'Python/Python312/Scripts/meson.exe' }
        $runnerBuild = Join-Path $project 'kernel/build/meson-qemu-runner'
        $runnerLog = Join-Path $Output ($Name + '.runner-build.log')
        if (!(Test-Path -LiteralPath (Join-Path $runnerBuild 'meson-private/build.dat'))) {
            & $meson setup $runnerBuild (Join-Path $project 'test/tooling/qemu-runner') --vsenv *> $runnerLog
            if ($LASTEXITCODE) { throw "Runner setup failed: $Name" }
        }
        & $meson compile -C $runnerBuild *>> $runnerLog
        if ($LASTEXITCODE) { throw "Runner build failed: $Name" }
        $program = Join-Path $runnerBuild 'qemu_runner.exe'
        $runnerArguments = $arguments
    } else {
        $arguments[0] = Join-Path $project $arguments[0]
        if ($entry.runner[0] -eq 'python') { $program = $python; $runnerArguments = $arguments }
        else { $program = 'powershell.exe'; $runnerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File') + $arguments }
    }
    Push-Location $project
    try {
        & $program @runnerArguments 1> $log 2> ($log + '.stderr')
        $code = $LASTEXITCODE
        if ($null -eq $code) { throw 'Runner exit code unavailable.' }
    } finally { Pop-Location }
} catch {
    $_.Exception.Message | Out-File $log -Append -Encoding utf8
    $code = 1
}
exit $code
