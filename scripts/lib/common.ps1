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
    $arguments = @('--enable=all', '--check-level=exhaustive', '--platform=win64', '--std=c11', '--xml', ('"-I' + $Project + '"')) +
        @($Defines | ForEach-Object { '-D' + $_ }) + @($Sources | ForEach-Object { '"' + $_ + '"' })
    $checked = Start-Process $checker -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardError "$Out/cppcheck.xml" -RedirectStandardOutput "$Out/cppcheck.log"
    if ($checked.ExitCode) { throw 'Cppcheck failed' }
    [xml]$report = Get-Content "$Out/cppcheck.xml"
    $blocking = @($report.results.errors.error | Where-Object { $_.severity -in 'error', 'warning', 'performance', 'portability' })
    if ($blocking.Count) { $blocking | ForEach-Object { "$($_.id): $($_.msg)" }; throw 'Blocking static diagnostics' }
}

# ============ serial evidence ============

function Read-Hex([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text, $Pattern)
    if (!$match.Success) { throw "Campo assente: $Pattern" }
    return [Convert]::ToUInt64($match.Groups[1].Value, 16)
}
