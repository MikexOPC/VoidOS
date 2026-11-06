# Symbol audit of a linked kernel map (B6). The production image must not contain test code:
# no object whose name derives from a test/ source (the map names them ...-test-... ) and none of
# the diagnostic entry points. A diagnostic profile may require specific symbols, which proves
# the audit actually sees them.
param(
    [Parameter(Mandatory)][string]$Map,
    [string[]]$Forbidden = @('x86_scheduler_probe','x86_scheduler_test_fault','x86_fb_drawing_probe'),
    [string[]]$Required = @(),
    [switch]$AllowTestObjects
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Map)) { throw "Map not found: $Map" }
$rows = Get-Content -LiteralPath $Map | Where-Object { $_ -match '^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+[0-9a-f]{16}\s+(?:f\s+)?(?:i\s+)?(\S+\.obj|\S+\.lib:\S+|\S+:\S+\.rcgu\.o)\s*$' } |
    ForEach-Object { [pscustomobject]@{ Symbol = $Matches[1]; Object = $Matches[2] } }
if ($rows.Count -lt 100) { throw "Map has only $($rows.Count) symbol rows; format not recognized" }
$names = @{}; foreach ($row in $rows) { $names[$row.Symbol] = $row.Object }
$problems = @()
foreach ($symbol in $Forbidden) { if ($names.ContainsKey($symbol)) { $problems += "forbidden symbol present: $symbol ($($names[$symbol]))" } }
foreach ($symbol in $Required) { if (!$names.ContainsKey($symbol)) { $problems += "required symbol missing: $symbol" } }
if (!$AllowTestObjects) {
    $testObjects = $rows | Where-Object { $_.Object -match '(^|-)test-' } | Select-Object -ExpandProperty Object -Unique
    foreach ($object in $testObjects) { $problems += "test object linked: $object" }
}
if ($problems.Count) { $problems | ForEach-Object { Write-Output "AUDIT FAIL: $_" }; throw "Symbol audit failed ($($problems.Count))" }
Write-Output "AUDIT PASS: $($rows.Count) symbols, $($Forbidden.Count) forbidden absent, $($Required.Count) required present, test objects $(if ($AllowTestObjects) {'allowed'} else {'none'}) ($Map)"
