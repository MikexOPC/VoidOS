param(
    [string[]]$Campaign,
    [switch]$Smoke,
    [switch]$List,
    [int]$Boots = 0,
    [int]$Parallel = 1
)
# G2 Q4: builds and runs the QEMU campaigns of campaigns.json by name. -Smoke runs one boot of
# every campaign (the check before a commit that touches the kernel). Every campaign runs even
# after a failure; the summary lists each result and the script fails if any failed.
# -Parallel N runs up to N campaigns at once (each has its own image and VM directories); the
# campaigns that build their own images in the shared tree (build null) run alone, first, and the
# heavy ones (VirtIO/IOMMU) one at a time.
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

$campaign = Join-Path $PSScriptRoot 'campaign.ps1'
$results = @()
$running = @()

function Complete-Campaign($job) {
    $job.Process.WaitForExit()
    $code = $job.Process.ExitCode
    $log = Join-Path $output ($job.Name + '.log')
    $script:results += [pscustomobject]@{ Campaign = $job.Name; Boots = $job.Count; ExitCode = $code; Log = $log }
    $script:results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    if ($code) {
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 15 }
        Write-Output "[ FAIL ] Campaign $($job.Name); log: $log"
    }
    else { Write-Output "[ OK ] Campaign $($job.Name); log: $log" }
}

# Shared-tree campaigns first and alone, then the others up to $Parallel at a time.
$ordered = @($selected | Where-Object { !$registry.$_.build }) + @($selected | Where-Object { $registry.$_.build })
foreach ($name in $ordered) {
    $entry = $registry.$name
    $count = if ($Boots) { $Boots } elseif ($Smoke) { $entry.smoke } else { $entry.boots }
    $limit = if ($entry.build) { [Math]::Max(1, $Parallel) } else { 1 }
    while ($running.Count -ge $limit -or (!$entry.build -and $running.Count) -or
        ($entry.heavy -and @($running | Where-Object { $registry.($_.Name).heavy }).Count)) {
        $done = @($running | Where-Object { $_.Process.HasExited })
        if (!$done) { Start-Sleep -Milliseconds 500; continue }
        foreach ($job in $done) { Complete-Campaign $job }
        $running = @($running | Where-Object { !$_.Process.HasExited })
    }
    Write-Output "[ INFO ] Campaign $name ($count boots)"
    [string[]]$campaignArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$campaign",
        '-Name', "$name", '-Count', "$count", '-Output', "$output")
    $process = Start-Process powershell.exe -ArgumentList $campaignArguments -WorkingDirectory $project -WindowStyle Hidden -PassThru
    # PowerShell 5.1: ExitCode stays empty unless the handle is opened while the process runs.
    $null = $process.Handle
    $running += [pscustomobject]@{ Name = $name; Count = $count; Process = $process }
}
foreach ($job in $running) { Complete-Campaign $job }

# ============ summary ============

$failed = @($results | Where-Object { $_.ExitCode })
"RESULT {0} qemu {1}/{2}; evidence {3}" -f $(if ($failed) { 'FAIL' } else { 'PASS' }), ($results.Count - $failed.Count), $results.Count, $output
if ($failed) { throw ('[ FAIL ] ' + (($failed | ForEach-Object Campaign) -join ', ')) }
