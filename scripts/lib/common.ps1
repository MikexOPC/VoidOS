# G2 Q1: helpers shared by the build and test scripts. Dot-source it:
#     . (Join-Path $project 'scripts/lib/common.ps1')

# ============ checked native execution ============

# Runs a native tool and throws on a non-zero exit code. With $Log (a file path) the command line
# and its output go to that log instead of the console; -Tee shows them as well.
function Invoke-OsChecked([string]$Program, [string[]]$Arguments, [string]$Log, [switch]$Tee) {
    if (!$Log) {
        & $Program @Arguments
        if ($LASTEXITCODE) { throw "Tool failed: $Program ($LASTEXITCODE)" }
        return
    }
    "$Program $($Arguments -join ' ')" | Out-File $Log -Append -Encoding utf8
    $messages = & $Program @Arguments 2>&1
    $code = $LASTEXITCODE
    $messages | Out-File $Log -Append -Encoding utf8
    if ($Tee) { $messages | Out-Host }
    if ($code) { throw "Tool failed: $Program ($code)" }
}

# The plain form used by most scripts; a script that logs defines its own one-line wrapper.
function Invoke-Checked([string]$Program, [string[]]$Arguments) { Invoke-OsChecked $Program $Arguments }

# ============ MSVC and Windows SDK environment ============

# Sets INCLUDE, LIB and PATH for the newest MSVC Build Tools and Windows SDK; returns the saved
# values for Restore-Msvc. The bin directory is in the result as .Bin.
function Use-Msvc {
    $vc = Get-ChildItem 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $sdk = 'C:/Program Files (x86)/Windows Kits/10'
    $version = Get-ChildItem "$sdk/Include" -Directory | Where-Object { Test-Path ($_.FullName + '/ucrt/stddef.h') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (!$vc -or !$version) { throw 'MSVC/SDK unavailable' }
    $saved = [pscustomobject]@{ Include = $env:INCLUDE; Lib = $env:LIB; Path = $env:PATH; Bin = $vc.FullName + '/bin/Hostx64/x64' }
    $env:PATH = $saved.Bin + ';' + $env:PATH
    $env:INCLUDE = $vc.FullName + '/include;' + $sdk + '/Include/' + $version.Name + '/ucrt;' +
        $sdk + '/Include/' + $version.Name + '/um;' + $sdk + '/Include/' + $version.Name + '/shared'
    $env:LIB = $vc.FullName + '/lib/x64;' + $sdk + '/Lib/' + $version.Name + '/ucrt/x64;' + $sdk + '/Lib/' + $version.Name + '/um/x64'
    return $saved
}

function Restore-Msvc($Saved) {
    if (!$Saved) { return }
    $env:INCLUDE = $Saved.Include; $env:LIB = $Saved.Lib; $env:PATH = $Saved.Path
}

# ============ static analysis ============

# cppcheck with the project profile; error, warning, performance and portability findings block.
function Invoke-Cppcheck([string]$Project, [string]$Out, [string[]]$Sources, [string[]]$Defines = @()) {
    $checker = "$Project/kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    # Test tiers: normal level by default (fast tier); OS_CPPCHECK_LEVEL=exhaustive for the full tier.
    # -j spreads the files over the cores (unusedFunction is skipped with -j; the full tier reports it).
    $arguments = @('--enable=all', "--check-level=$OsCppcheckLevel", "-j$OsCppcheckJobs","--quiet", '--platform=win64', '--std=c11', '--xml', ('"-I' + $Project + '"')) +
        @($Defines | ForEach-Object { '-D' + $_ }) + @($Sources | ForEach-Object { '"' + $_ + '"' })
    $checked = Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$Out/cppcheck.xml" -RedirectStandardOutput "$Out/cppcheck.log"
    if ($checked.ExitCode) { throw 'Cppcheck failed' }
    [xml]$report = Get-Content "$Out/cppcheck.xml"
    $blocking = @($report.results.errors.error | Where-Object { $_.severity -in 'error', 'warning', 'performance', 'portability' })
    if ($blocking.Count) { $blocking | ForEach-Object { "$($_.id): $($_.msg)" }; throw 'Blocking static diagnostics' }
}

# ============ test tiers ============

# Fast tier by default: cppcheck normal level over all cores. OS_CPPCHECK_LEVEL=exhaustive for the full
# tier. Dot-sourcing puts these in the caller's scope, so every suite script reads the same choice.
$OsCppcheckLevel = if ($env:OS_CPPCHECK_LEVEL -eq 'exhaustive') { 'exhaustive' } else { 'normal' }
$OsCppcheckJobs = [Math]::Max(1, [Environment]::ProcessorCount)

# ====== Invoke-CppcheckRaw ======

# Plain function on purpose: $args keeps tokens such as --enable=all as arguments. The first one is
# the XML report path. Start-Process with file redirection: the '& exe 2> file' form of PowerShell 5.1
# left cppcheck blocked on its output pipe. Sets LASTEXITCODE like a native call.
function Invoke-CppcheckRaw {
    $xml = $args[0]
    $checker = "$PSScriptRoot/../../kernel/build/full-review-2026-10-05/tools/cppcheck-2.22.0/build-ninja/bin/cppcheck.exe"
    $arguments = @($args | Select-Object -Skip 1 | ForEach-Object { $_ } | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } })
    $checked = Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError $xml -RedirectStandardOutput ($xml + '.log')
    $global:LASTEXITCODE = $checked.ExitCode
}

# ============ serial evidence ============

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text, $Pattern)
    if (!$match.Success) { throw "Campo assente: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value, 16)
}

# ============ tool discovery ============

# Python 3 and PowerShell 7 without paths tied to one machine. Order: the VOIDOS_PYTHON or
# VOIDOS_PWSH variable, then PATH, then the standard install location. The WindowsApps alias that
# opens the Store is skipped.
function Resolve-OsPython {
    if ($env:VOIDOS_PYTHON) {
        if (Test-Path -LiteralPath $env:VOIDOS_PYTHON -PathType Leaf) { return $env:VOIDOS_PYTHON }
        throw "VOIDOS_PYTHON points to a missing file: $env:VOIDOS_PYTHON"
    }
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        $found = & $launcher.Source -3 -c 'import sys; print(sys.executable)' 2>$null
        if ($LASTEXITCODE -eq 0 -and $found -and (Test-Path -LiteralPath $found -PathType Leaf)) { return [string]$found }
    }
    foreach ($command in @(Get-Command python.exe -All -ErrorAction SilentlyContinue)) {
        if ($command.Source -notlike '*\WindowsApps\*') { return $command.Source }
    }
    throw 'Python 3 not found. Install it, add it to PATH or set VOIDOS_PYTHON.'
}

function Resolve-OsPwsh {
    if ($env:VOIDOS_PWSH) {
        if (Test-Path -LiteralPath $env:VOIDOS_PWSH -PathType Leaf) { return $env:VOIDOS_PWSH }
        throw "VOIDOS_PWSH points to a missing file: $env:VOIDOS_PWSH"
    }
    $command = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $standard = Join-Path $env:ProgramFiles 'PowerShell/7/pwsh.exe'
    if (Test-Path -LiteralPath $standard -PathType Leaf) { return $standard }
    throw 'PowerShell 7 not found. Install it, add it to PATH or set VOIDOS_PWSH.'
}
