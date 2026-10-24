param(
    [string[]]$Campaign,
    [switch]$Smoke,
    [switch]$List,
    [int]$Boots = 0
)
# G2 Q4: builds and runs the QEMU campaigns of campaigns.json by name. -Smoke runs one boot of
# every campaign (the check before a commit that touches the kernel). Every campaign runs even
# after a failure; the summary lists each result and the script fails if any failed.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$registry = (Get-Content -Raw (Join-Path $PSScriptRoot 'campaigns.json') | ConvertFrom-Json).campaigns
$names = @($registry.PSObject.Properties.Name)
$python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'

# ============ inventory ============

if ($List) {
    foreach ($name in $names) { '{0,-18} {1,3} boots, smoke {2}: {3}' -f $name, $registry.$name.boots, $registry.$name.smoke, $registry.$name.summary }
    return
}
# 'powershell -File' passes "a,b" as one string: split it here so both call styles work.
$selected = if ($Smoke -and !$Campaign) { $names } else { @($Campaign | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) }
if (!$selected) { throw 'Name a campaign (-Campaign v2c), use -Smoke or -List' }
foreach ($name in $selected) { if ($names -notcontains $name) { throw "Unknown campaign: $name (use -List)" } }
$output = Join-Path $project ('kernel/build/qemu-run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null

# ============ campaigns ============

$results = @()
foreach ($name in $selected) {
    $entry = $registry.$name
    $count = if ($Boots) { $Boots } elseif ($Smoke) { $entry.smoke } else { $entry.boots }
    $log = Join-Path $output ($name + '.log')
    $image = ''
    $code = 0
    Write-Output "[ INFO ] Campaign $name ($count boots)"
    try {
        if ($entry.build) {
            $images = Join-Path $output $name
            $build = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $project 'kernel/scripts/build.ps1')) +
                @($entry.build) + @('-OutputDirectory', $images)
            & powershell.exe @build *> (Join-Path $output ($name + '.build.log'))
            if ($LASTEXITCODE) { throw "Build failed: $name" }
            $image = Join-Path $images 'kernel.elf'
        }
        $arguments = @($entry.runner | Select-Object -Skip 1 | ForEach-Object {
            $_.Replace('{image}', $image).Replace('{boots}', [string]$count) })
        $arguments[0] = Join-Path $project $arguments[0]
        # Start-Process with file redirection: UTF-8 logs (PowerShell 5.1 '*>' writes UTF-16) and no
        # NativeCommandError from the runner's stderr.
        $quoted = @($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
        # Not $list: PowerShell names are case-insensitive and -List is a [switch] parameter.
        if ($entry.runner[0] -eq 'python') { $program = $python; $runnerArguments = $quoted }
        else { $program = 'powershell.exe'; $runnerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File') + $quoted }
        $start = @{
            FilePath = $program; ArgumentList = $runnerArguments; WorkingDirectory = $project; WindowStyle = 'Hidden'
            Wait = $true; PassThru = $true; RedirectStandardOutput = $log; RedirectStandardError = $log + '.stderr'
        }
        $process = Start-Process @start
        $code = $process.ExitCode
    } catch {
        $_.Exception.Message | Out-File $log -Append -Encoding utf8
        $code = 1
    }
    $results += [pscustomobject]@{ Campaign = $name; Boots = $count; ExitCode = $code; Log = $log }
    $results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    if ($code) {
        Get-Content -LiteralPath $log -Tail 15
        Write-Output "[ FAIL ] Campaign $name; log: $log"
    }
    else { Write-Output "[ OK ] Campaign $name; log: $log" }
}

# ============ summary ============

$failed = @($results | Where-Object { $_.ExitCode })
"RESULT {0} qemu {1}/{2}; evidence {3}" -f $(if ($failed) { 'FAIL' } else { 'PASS' }), ($results.Count - $failed.Count), $results.Count, $output
if ($failed) { throw ('[ FAIL ] ' + (($failed | ForEach-Object Campaign) -join ', ')) }
