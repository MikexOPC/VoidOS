param(
    [string[]]$Campaign,
    [switch]$Smoke,
    [switch]$List,
    [int]$Boots = 0,
    [int]$Parallel = 1,
    [int]$BuildParallel = 4,
    [switch]$Fresh
)
# G2 Q4: builds and runs the QEMU campaigns of campaigns.json by name. -Smoke runs one boot of
# every campaign (the check before a commit that touches the kernel). Every campaign runs even
# after a failure; the summary lists each result and the script fails if any failed.
# -Parallel N runs up to N campaigns at once (each has its own image and VM directories); the
# campaigns that build their own images in the shared tree (build null) run alone, first, and the
# heavy ones (VirtIO/IOMMU) one at a time.
# Images are built first, once per distinct build, up to -BuildParallel at once, and reused while the
# sources are unchanged. A campaign is skipped when the same image, runner, loader, OVMF, QEMU and
# entry already passed with the same boots; -Fresh runs everything again.
$ErrorActionPreference = 'Stop'
$campaignShell = (Get-Command pwsh.exe -ErrorAction SilentlyContinue).Source
if (!$campaignShell) {
    foreach ($candidate in @("$env:ProgramFiles/PowerShell/7/pwsh.exe",
        "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe")) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $campaignShell = $candidate; break }
    }
}
if (!$campaignShell) { throw 'PowerShell 7 required for QEMU campaigns, as for host suites.' }
if ($PSVersionTable.PSVersion.Major -lt 7) { throw "Run with PowerShell 7: & '$campaignShell' -File '$PSCommandPath'" }
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

# ============ fingerprints ============

function Get-TextHash([string]$Text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    -join ([Security.Cryptography.SHA256]::HashData($bytes) | ForEach-Object { $_.ToString('x2') })
}

function Get-FilesHash([string[]]$Paths) {
    Get-TextHash ((@($Paths | Sort-Object -Unique | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { $_ + '=' + (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })) -join "`n")
}

function Get-BuildKey($Entry) {
    if ($Entry.meson) { 'meson:' + $Entry.meson } else { 'build:' + ($Entry.build | ConvertTo-Json -Compress) }
}

# Sources outside docs/ and the build tree: commit, tracked changes and untracked files.
Push-Location $project
try {
    $head = git rev-parse HEAD
    $changes = (git diff HEAD --binary -- . ':(exclude)docs') -join "`n"
    $untracked = @(git ls-files --others --exclude-standard -- . ':(exclude)docs')
} finally { Pop-Location }
$sources = Get-TextHash ($head + "`n" + $changes + "`n" + (Get-FilesHash @($untracked | ForEach-Object { Join-Path $project $_ })))
$qemuDir = 'C:/Program Files/qemu'
$platform = Get-FilesHash @("$qemuDir/qemu-system-x86_64.exe", "$qemuDir/share/edk2-x86_64-code.fd",
    "$qemuDir/share/edk2-i386-vars.fd", (Join-Path $project 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'))
$cacheRoot = Join-Path $project 'kernel/build/campaign-cache'
$passFile = Join-Path $cacheRoot 'passes.json'
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$passes = @{}
if (!$Fresh -and (Test-Path -LiteralPath $passFile)) {
    (Get-Content -Raw -LiteralPath $passFile | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $passes[$_.Name] = $_.Value }
}

# ============ campaigns ============

$campaign = Join-Path $PSScriptRoot 'campaign.ps1'
$results = @()
$running = @()

function Record-Campaign([string]$Name, [int]$Count, [int]$Code, [string]$Pass = '') {
    $log = Join-Path $output ($Name + '.log')
    $reportPattern = '^# ====== Boot [0-9]+ ====== #$|^\[ (Pass|Fail) \]$|^Causa: |^Boot non eseguiti: '
    $script:results += [pscustomobject]@{ Campaign = $Name; Boots = $Count; ExitCode = $Code; Log = $log; Skipped = $false }
    $script:results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    if (!$Code -and $Pass) {
        $script:passes[$Pass] = $Name
        $script:passes | ConvertTo-Json | Set-Content -LiteralPath $passFile -Encoding utf8
    }
    if (Test-Path -LiteralPath $log) {
        foreach ($line in Get-Content -LiteralPath $log) {
            if ($line -eq '[ Pass ]') { Write-Host $line -ForegroundColor Green }
            elseif ($line -eq '[ Fail ]') { Write-Host $line -ForegroundColor Red }
            elseif ($line -match '^# ====== Boot [0-9]+ ====== #$|^Causa: |^Boot non eseguiti: ') {
                Write-Output $line
            }
        }
    }
    if ($Code) {
        if (Test-Path -LiteralPath $log) {
            Get-Content -LiteralPath $log -Tail 15 | Where-Object { $_ -notmatch $reportPattern }
        }
        Write-Output "[ FAIL ] Campaign $Name; log: $log"
    }
    else { Write-Output "[ OK ] Campaign $Name; log: $log" }
}

function Start-Campaign([string[]]$Extra) {
    [string[]]$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$campaign") + $Extra
    $process = Start-Process $campaignShell -ArgumentList $arguments -WorkingDirectory $project -WindowStyle Hidden -PassThru
    # PowerShell 5.1: ExitCode stays empty unless the handle is opened while the process runs.
    $null = $process.Handle
    $process
}

# ============ images ============

$images = [ordered]@{}
foreach ($name in $selected) {
    $entry = $registry.$name
    if ($null -eq $entry.build) { continue }
    $key = Get-BuildKey $entry
    if ($images.Contains($key)) { continue }
    $digest = Get-TextHash ($sources + "`n" + $key)
    $directory = Join-Path $output $name
    $images[$key] = [pscustomobject]@{ Name = $name; Directory = $directory; Image = (Join-Path $cacheRoot "elf-$digest.elf"); Process = $null; Ready = $false }
}
$building = @()
foreach ($job in $images.Values) {
    if (Test-Path -LiteralPath $job.Image) { $job.Ready = $true; continue }
    while (@($building | Where-Object { !$_.Process.HasExited }).Count -ge [Math]::Max(1, $BuildParallel)) { Start-Sleep -Milliseconds 300 }
    Write-Output "[ INFO ] Build $($job.Name)"
    $job.Process = Start-Campaign @('-Name', $job.Name, '-Count', '1', '-Output', $output, '-BuildOnly', '-Images', $job.Directory)
    $building += $job
}
foreach ($job in $building) {
    $job.Process.WaitForExit()
    $builtImage = Join-Path $job.Directory 'kernel.elf'
    if ($job.Process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $builtImage -PathType Leaf)) { continue }
    $pendingImage = Join-Path $cacheRoot ("elf-" + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::Copy($builtImage, $pendingImage)
        try { [IO.File]::Move($pendingImage, $job.Image) }
        catch [IO.IOException] {
            if (!(Test-Path -LiteralPath $job.Image -PathType Leaf)) { throw }
        }
        $job.Ready = $true
    } finally {
        [IO.File]::Delete($pendingImage)
    }
}
$builtCount = $building.Count
Write-Output "[ INFO ] Images: $($images.Count) distinct, $builtCount built, $($images.Count - $builtCount) reused"

# ============ runners ============

$runnerBuild = Join-Path $project 'kernel/build/meson-qemu-runner'
if (@($selected | Where-Object { $registry.$_.runner[0] -eq 'qemu-runner' }).Count) {
    $meson = (Get-Command meson -ErrorAction SilentlyContinue).Source
    if (!$meson) { $meson = Join-Path $env:APPDATA 'Python/Python312/Scripts/meson.exe' }
    if (!(Test-Path -LiteralPath (Join-Path $runnerBuild 'meson-private/build.dat'))) {
        & $meson setup $runnerBuild (Join-Path $project 'test/tooling/qemu-runner') --vsenv *> (Join-Path $output 'runner-build.log')
    }
    & $meson compile -C $runnerBuild *>> (Join-Path $output 'runner-build.log')
    if ($LASTEXITCODE) { throw 'Runner build failed.' }
}
Push-Location $project
try { $runnerSources = @(git ls-files -co --exclude-standard -- '*.py' | ForEach-Object { Join-Path $project $_ }) }
finally { Pop-Location }

function Get-RunnerHash($Entry) {
    if ($Entry.runner[0] -eq 'qemu-runner') { return Get-FilesHash @(Join-Path $runnerBuild 'qemu_runner.exe') }
    # Python runners load modules from other folders by path: every tracked runner file counts.
    Get-FilesHash $runnerSources
}

# ============ schedule ============

# Shared-tree campaigns first and alone; then any campaign whose limits allow it, not strictly in order.
$pending = [Collections.Generic.List[string]]::new()
@($selected | Where-Object { $null -eq $registry.$_.build }) + @($selected | Where-Object { $null -ne $registry.$_.build }) |
    ForEach-Object { $pending.Add($_) }

function Test-Startable([string]$Name) {
    $entry = $registry.$Name
    if ($null -eq $entry.build) { return $script:running.Count -eq 0 }
    if (@($script:running | Where-Object { $null -eq $registry.($_.Name).build }).Count) { return $false }
    if ($script:running.Count -ge [Math]::Max(1, $Parallel)) { return $false }
    !($entry.heavy -and @($script:running | Where-Object { $registry.($_.Name).heavy }).Count)
}

while ($pending.Count -or $running.Count) {
    $finished = @($running | Where-Object { $_.Process.HasExited })
    foreach ($job in $finished) {
        $job.Process.WaitForExit()
        Record-Campaign $job.Name $job.Count $job.Process.ExitCode $job.Pass
    }
    $running = @($running | Where-Object { $finished -notcontains $_ })
    $next = $null
    foreach ($candidate in $pending) {
        if (Test-Startable $candidate) { $next = $candidate; break }
        if ($null -eq $registry.$candidate.build) { break }
    }
    if ($null -eq $next) { Start-Sleep -Milliseconds 300; continue }
    $null = $pending.Remove($next)
    $entry = $registry.$next
    $count = if ($Boots) { $Boots } elseif ($Smoke) { $entry.smoke } else { $entry.boots }
    $extra = @('-Name', $next, '-Count', "$count", '-Output', $output)
    $pass = ''
    if ($null -ne $entry.build) {
        $image = $images[(Get-BuildKey $entry)]
        if (!$image.Ready) {
            "Build failed: $next" | Out-File (Join-Path $output ($next + '.log')) -Encoding utf8
            Record-Campaign $next $count 1
            continue
        }
        $extra += @('-Image', $image.Image)
        if ($entry.runner[0] -ne 'powershell') {
            $pass = Get-TextHash ((@($next, $count, ($entry | ConvertTo-Json -Compress -Depth 5),
                (Get-FileHash -LiteralPath $image.Image -Algorithm SHA256).Hash, (Get-RunnerHash $entry), $platform)) -join "`n")
            if ($passes.ContainsKey($pass)) {
                $script:results += [pscustomobject]@{ Campaign = $next; Boots = $count; ExitCode = 0; Log = ''; Skipped = $true }
                $results | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
                Write-Output "[ SKIP ] Campaign ${next}: already passed on the same image and runner"
                continue
            }
        }
    }
    Write-Output "[ INFO ] Campaign $next ($count boots)"
    $running += [pscustomobject]@{ Name = $next; Count = $count; Pass = $pass; Process = (Start-Campaign $extra) }
}

# ============ summary ============

$failed = @($results | Where-Object { $_.ExitCode })
$skipped = @($results | Where-Object { $_.Skipped }).Count
"RESULT {0} qemu {1}/{2}, {3} skipped as already passed; evidence {4}" -f $(if ($failed) { 'FAIL' } else { 'PASS' }),
    ($results.Count - $failed.Count), $results.Count, $skipped, $output
if ($failed) { throw ('[ FAIL ] ' + (($failed | ForEach-Object Campaign) -join ', ')) }
