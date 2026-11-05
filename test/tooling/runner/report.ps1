param()
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$output = Join-Path $project ('kernel/build/report-regression-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null

# ============ campaign reporting without starting a VM ============

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $project 'scripts/test/qemu.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'QEMU runner syntax error' }
$record = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Record-Campaign'
}, $true)
if (!$record) { throw 'Campaign reporter missing' }
. ([scriptblock]::Create($record.Extent.Text))
$script:results = @()
foreach ($case in @(@{ Name = 'pass'; Code = 0; Label = '[ Pass ]' },
                   @{ Name = 'fail'; Code = 7; Label = '[ Fail ]' })) {
    $lines = @('# ====== Boot 1 ====== #', $case.Label)
    if ($case.Code) { $lines += 'Causa: synthetic timeout' }
    $lines | Set-Content -LiteralPath (Join-Path $output ($case.Name + '.log')) -Encoding utf8
    $rendered = (& { Record-Campaign $case.Name 1 $case.Code } 6>&1 | Out-String)
    if (!$rendered.Contains('# ====== Boot 1 ====== #') -or !$rendered.Contains($case.Label)) {
        throw 'Per-boot result missing from campaign output'
    }
    if ($case.Code -and !$rendered.Contains('Causa: synthetic timeout')) {
        throw 'Failure cause missing from campaign output'
    }
    if ([regex]::Matches($rendered, '(?m)^' + [regex]::Escape($case.Label) + '\r?$').Count -ne 1) {
        throw 'Boot result duplicated in campaign output'
    }
}
$report = @(Get-Content -Raw -LiteralPath (Join-Path $output 'results.json') | ConvertFrom-Json)
if ($report.Count -ne 2 -or $report[0].ExitCode -ne 0 -or $report[1].ExitCode -ne 7) {
    throw 'Reporting changed campaign exit codes'
}
Write-Output 'PASS: synthetic boot reports forwarded and campaign exit codes preserved'
