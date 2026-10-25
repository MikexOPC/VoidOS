param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][int]$Count,
    [Parameter(Mandatory)][string]$Output
)
# One campaign of campaigns.json: build its image into $Output/$Name, run the runner, exit with its
# code. Called by qemu.ps1, sequentially or several at once (each campaign has its own directories).
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$entry = (Get-Content -Raw (Join-Path $PSScriptRoot 'campaigns.json') | ConvertFrom-Json).campaigns.$Name
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$log = Join-Path $Output ($Name + '.log')
$image = ''
$code = 0

# ============ build and run ============

try {
    if ($entry.build) {
        $images = Join-Path $Output $Name
        $build = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $project 'kernel/scripts/build.ps1')) +
            @($entry.build) + @('-OutputDirectory', $images)
        & powershell.exe @build *> (Join-Path $Output ($Name + '.build.log'))
        if ($LASTEXITCODE) { throw "Build failed: $Name" }
        $image = Join-Path $images 'kernel.elf'
    }
    $arguments = @($entry.runner | Select-Object -Skip 1 | ForEach-Object {
        $_.Replace('{image}', $image).Replace('{boots}', [string]$Count) })
    $arguments[0] = Join-Path $project $arguments[0]
    # Start-Process with file redirection: UTF-8 logs (PowerShell 5.1 '*>' writes UTF-16) and no
    # NativeCommandError from the runner's stderr.
    $quoted = @($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    if ($entry.runner[0] -eq 'python') { $program = $python; $runnerArguments = $quoted }
    else { $program = 'powershell.exe'; $runnerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File') + $quoted }
    $start = @{
        FilePath = $program; ArgumentList = $runnerArguments; WorkingDirectory = $project; WindowStyle = 'Hidden'
        Wait = $true; PassThru = $true; RedirectStandardOutput = $log; RedirectStandardError = $log + '.stderr'
    }
    $code = (Start-Process @start).ExitCode
} catch {
    $_.Exception.Message | Out-File $log -Append -Encoding utf8
    $code = 1
}
exit $code
