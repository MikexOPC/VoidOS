param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$audit = "$project/arch/x86/kernel/cpu/percpu/panic/scripts/audit.ps1"
$fixture = "$PSScriptRoot/assembly/audit/fixture.asm"
$tool = & "$project/arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1"
foreach ($kind in @('scalar','simd','x87','avx')) {
    $defines = if ($kind -eq 'scalar') { @() } else { @('-DAUDIT_' + $kind.ToUpperInvariant() + '=1') }
    Invoke-Checked $tool.Nasm (@('-f','win64','-Wall','-Werror','-o',"$out/$kind.obj",$fixture) + $defines)
}
$shell = (Get-Process -Id $PID).Path
$icr = 'arch-x86-kernel-interrupts-apic-icr-icr.obj'
$names = @([regex]::Matches((Get-Content -LiteralPath $audit -Raw), "'([^']+\.obj)'") |
    ForEach-Object { $_.Groups[1].Value })
$names = @(@($names) + @($icr) | Select-Object -Unique)

# ============ actual COFF disassembly, including the shared ICR dependency ============

foreach ($case in @('scalar','simd','x87','avx','missing')) {
    $directory = Join-Path $out $case
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    foreach ($name in $names) {
        if ($case -eq 'missing' -and $name -eq $icr) { continue }
        $object = if ($case -in @('simd','x87','avx') -and $name -eq $icr) { "$out/$case.obj" } else { "$out/scalar.obj" }
        Copy-Item -LiteralPath $object -Destination (Join-Path $directory $name)
    }
    $log = Join-Path $out "$case.log"
    $errorLog = Join-Path $out "$case.stderr.log"
    $process = Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass',
        '-File',('"' + $audit + '"'),'-ImageDirectory',('"' + $directory + '"')) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $errorLog
    try {
        $null = $process.Handle
        if (!$process.WaitForExit(30000)) {
            if (!$process.HasExited) { $process.Kill() }
            throw "Audit fixture exceeded its deadline: $case"
        }
        $code = $process.ExitCode
    } finally { $process.Dispose() }
    $errors = Get-Content -LiteralPath $errorLog -Raw
    if ($case -eq 'scalar') {
        if ($code -ne 0) { throw "Scalar audit unexpectedly failed: $errors" }
        if (!(Test-Path -LiteralPath (Join-Path $directory ($icr + '.asm.txt')))) {
            throw 'The audit did not disassemble the ICR helper'
        }
    } else {
        $reason = if ($case -eq 'missing') { 'Cannot disassemble' } else { 'FP/SIMD dependency' }
        if ($null -eq $code -or $code -eq 0 -or !$errors.Contains($reason) -or !$errors.Contains($icr)) {
            throw "Audit failed to reject the $case ICR fixture: $errors"
        }
    }
    "AUDIT_FIXTURE_PASS: $case"
}
