param(
    [ValidateSet('all','boot','kernel','bootstrap','scheduler','x86-scheduler','paging','ps2','framebuffer','x86-framebuffer','console','hpet','watchdog')]
    [string]$Suite = 'all',
    [switch]$List
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$suites = [ordered]@{
    boot = 'arch/x86/boot/uefi/scripts/build.ps1'
    kernel = 'test/kernel/host.ps1'
    bootstrap = 'test/kernel/bootstrap/scripts/host.ps1'
    scheduler = 'kernel/scheduler/scripts/test.ps1'
    'x86-scheduler' = 'arch/x86/kernel/scheduler/scripts/test.ps1'
    paging = 'arch/x86/mm/paging/scripts/test.ps1'
    ps2 = 'kernel/drivers/input/ps2/scripts/test.ps1'
    framebuffer = 'kernel/drivers/video/framebuffer/scripts/test.ps1'
    'x86-framebuffer' = 'arch/x86/kernel/drivers/video/framebuffer/scripts/test.ps1'
    console = 'kernel/console/video/scripts/test.ps1'
    hpet = 'arch/x86/kernel/timer/hpet/description/scripts/test.ps1'
    watchdog = 'arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1'
}

# ============ resolve supported suite shell ============

function Resolve-SuiteShell {
    $runtime = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($runtime) { return $runtime.Source }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'PowerShell/7/pwsh.exe'),
        (Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'PowerShell 7 (pwsh.exe) required for host suites. Add it to PATH before running tests.'
}

# ============ isolated child process ============

function Invoke-IsolatedSuite([string]$Shell, [string]$Script, [string]$Log, [string]$ErrorLog) {
    $process = Start-Process -FilePath $Shell -ArgumentList @('-NoProfile','-File',('"' + $Script + '"')) `
        -WorkingDirectory $project -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $Log -RedirectStandardError $ErrorLog
    try {
        $null = $process.Handle
        $process.WaitForExit()
        if ($null -eq $process.ExitCode) { throw 'Child exit code unavailable.' }
        return $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

# ============ suite inventory ============

if ($List) {
    $suites.GetEnumerator() | ForEach-Object { '{0}: {1}' -f $_.Key, $_.Value }
    return
}
$output = Join-Path $project ('kernel/build/test-run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$shell = Resolve-SuiteShell
$selected = if ($Suite -eq 'all') { @($suites.Keys) } else { @($Suite) }
$results = @()

# ============ isolated supported toolchain runners ============

foreach ($name in $selected) {
    $script = Join-Path $project $suites[$name]
    $log = Join-Path $output ($name + '.log')
    $errorLog = Join-Path $output ($name + '.stderr.log')
    Write-Output "[ INFO ] Suite $name"
    $code = Invoke-IsolatedSuite $shell $script $log $errorLog
    $results += [pscustomobject]@{ Suite = $name; ExitCode = $code; Log = $log; ErrorLog = $errorLog }
    $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    if ($code -ne 0) {
        Get-Content -LiteralPath $log -Tail 20
        Get-Content -LiteralPath $errorLog -Tail 20
        throw "[ FAIL ] Suite $name; logs: $log, $errorLog"
    }
    Write-Output "[ OK ] Suite $name; log: $log"
}
Write-Output "Host suites completed: $output. QEMU campaigns are separate."
