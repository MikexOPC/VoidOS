$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$build = Join-Path $project 'kernel/scripts/build.ps1'
$source = Join-Path $project 'scripts/tooling/build-plan/rust/plan/main.rs'
$output = Join-Path $project ('kernel/build/build-plan-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$planner = Join-Path $output 'build-plan.exe'
& rustc.exe --edition=2024 -D warnings $source -o $planner
if ($LASTEXITCODE -ne 0) { throw 'Rust planner compilation failed.' }

# ====== Assert-PlanParity ======

function Assert-PlanParity([string]$Name, [hashtable]$Options, [string[]]$Arguments) {
    $reference = @(& $build -PlanOnly @Options)
    $actual = @(& $planner @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Rust plan failed for $Name." }
    $reference | Set-Content -LiteralPath (Join-Path $output "$Name-powershell.tsv")
    $actual | Set-Content -LiteralPath (Join-Path $output "$Name-rust.tsv")
    if ($reference.Count -lt 3 -or $reference.Count -ne $actual.Count) {
        throw "Different command counts for ${Name}: PowerShell $($reference.Count), Rust $($actual.Count)."
    }
    for ($index = 0; $index -lt $reference.Count; ++$index) {
        if (![string]::Equals($reference[$index], $actual[$index], [StringComparison]::Ordinal)) {
            throw "Plan mismatch for $Name at command $($index + 1). Compare the TSV files in $output."
        }
    }
    Write-Host "[ Pass ] $Name ($($actual.Count) commands)"
}

# ====== Assert-PlanRejected ======

function Assert-PlanRejected([string]$Name, [hashtable]$Options, [string[]]$Arguments, [string]$Expected) {
    $reference = [System.Collections.Generic.List[string]]::new()
    $message = $null
    try { & $build -PlanOnly @Options | ForEach-Object { $reference.Add($_) } }
    catch { $message = $_.Exception.Message }
    $errorLog = Join-Path $output "$Name-rust.stderr.log"
    $actual = @(& $planner @Arguments 2> $errorLog)
    $exitCode = $LASTEXITCODE
    $rustError = (Get-Content -LiteralPath $errorLog -Raw).Trim()
    if ($reference.Count -ne 0 -or $message -cne $Expected -or
        $actual.Count -ne 0 -or $exitCode -eq 0 -or $rustError -cne $Expected) {
        throw "Unexpected rejection for ${Name}. PowerShell: '$message'. Rust (${exitCode}): '$rustError'."
    }
    Write-Host "[ Pass ] $Name (rejected without emitting commands)"
}

Assert-PlanParity 'ordinary' @{} @()
Assert-PlanParity 'ordinary-explicit' @{TestApService = 'none'} @('-TestApService', 'none')
Assert-PlanParity 'bsp-periodic' @{TestSchedulerPeriodic = $true} @('-TestSchedulerPeriodic')
Assert-PlanParity 'ap-timer' @{TestApService = 'ap-timer'} @('-TestApService', 'ap-timer')
Assert-PlanParity 'k6-process' @{TestK6Process = $true} @('-TestK6Process')
foreach ($case in 31..37) {
    Assert-PlanParity "k6-process-$case" @{TestK6Process = $true; TestK6Negative = $case} @('-TestK6Process', '-TestK6Negative', [string]$case)
}

Assert-PlanRejected 'conflicting-preemption' @{TestSchedulerPeriodic = $true; TestSchedulerPreemption = $true} `
    @('-TestSchedulerPeriodic', '-TestSchedulerPreemption') 'Choose one preemption diagnostic.'
Assert-PlanRejected 'periodic-with-ap-timer' @{TestSchedulerPeriodic = $true; TestApService = 'ap-timer'} `
    @('-TestSchedulerPeriodic', '-TestApService', 'ap-timer') 'Preemption requires an isolated diagnostic profile.'
Assert-PlanRejected 'ap-timer-with-ps2' @{TestApService = 'ap-timer'; Ps2Input = $true} `
    @('-TestApService', 'ap-timer', '-Ps2Input') 'AP service requires an isolated diagnostic profile.'
Assert-PlanRejected 'unsupported-ap-ping' @{TestApService = 'ping'} `
    @('-TestApService', 'ping') 'Unsupported plan-only options.'
Assert-PlanRejected 'unsupported-install' @{Install = $true} @('-Install') 'Unsupported plan-only options.'
Assert-PlanRejected 'k6-negative-low' @{TestK6Process = $true; TestK6Negative = 30} `
    @('-TestK6Process', '-TestK6Negative', '30') 'Invalid K6 negative case.'
Assert-PlanRejected 'k6-negative-high' @{TestK6Process = $true; TestK6Negative = 38} `
    @('-TestK6Process', '-TestK6Negative', '38') 'Invalid K6 negative case.'
Assert-PlanRejected 'k6-negative-alone' @{TestK6Negative = 31} @('-TestK6Negative', '31') 'Invalid K6 negative case.'
Assert-PlanRejected 'k6-negative-with-ps2' @{TestK6Process = $true; TestK6Negative = 31; Ps2Input = $true} `
    @('-TestK6Process', '-TestK6Negative', '31', '-Ps2Input') 'K6 process requires an isolated diagnostic profile.'

# ====== Assert-RustBuildRejected ======

function Assert-RustBuildRejected([hashtable]$Options, [string]$Expected) {
    $message = $null
    try { & $build -UseRustPlan @Options | Out-Null }
    catch { $message = $_.Exception.Message }
    if ($message -cne $Expected) { throw "Unexpected Rust build rejection: '$message'." }
}

$unusedOutput = Join-Path $output 'must-not-be-created'
$scopeError = 'Rust build plan requires a supported profile and a new OutputDirectory.'
Assert-RustBuildRejected @{} $scopeError
Assert-RustBuildRejected @{OutputDirectory = $unusedOutput; TestApService = 'ping'} $scopeError
Assert-RustBuildRejected @{OutputDirectory = $unusedOutput; Install = $true} $scopeError
Assert-RustBuildRejected @{OutputDirectory = $unusedOutput; PlanOnly = $true} $scopeError
Assert-RustBuildRejected @{OutputDirectory = $unusedOutput; TestSchedulerPeriodic = $true; TestApService = 'ap-timer'} `
    'Preemption requires an isolated diagnostic profile.'
Assert-RustBuildRejected @{OutputDirectory = $unusedOutput; TestApService = 'ap-timer'; Ps2Input = $true} `
    'AP service requires an isolated diagnostic profile.'
Assert-RustBuildRejected @{OutputDirectory = $output} 'Rust build plan output already exists.'
if (Test-Path -LiteralPath $unusedOutput) { throw 'Rejected build created an output directory.' }
Write-Host '[ Pass ] Seven opt-in build rejection cases'
Write-Host "[ Pass ] Plan evidence: $output"
