param(
    [int]$Keep = 3,
    [switch]$Apply
)
# Fase 0: kernel/build cleanup. Only run directories (<name>-<32 hex id>) are candidates; fixed
# directories (tools, layouts, host outputs without an id) are never touched. Per name, the newest
# $Keep runs stay, plus every run cited in the tracked documents and every run that a kept run's
# logs or results name (the nested evidence of a campaign). Dry run unless -Apply.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $project 'kernel/build'
$pattern = '^(?<name>.+)-(?<id>[0-9a-f]{32})$'

# ============ candidates ============

$runs = @(Get-ChildItem -LiteralPath $build -Directory | Where-Object { $_.Name -match $pattern } |
    ForEach-Object { [pscustomobject]@{ Dir = $_; Name = ($_.Name -replace '-[0-9a-f]{32}$', '') } })

# ============ kept: newest per name ============

$kept = @{}
foreach ($group in ($runs | Group-Object Name)) {
    $group.Group | Sort-Object { $_.Dir.LastWriteTime } -Descending | Select-Object -First $Keep |
        ForEach-Object { $kept[$_.Dir.Name] = 'newest' }
}

# ============ kept: cited in tracked documents ============

$idRegex = '[A-Za-z0-9][A-Za-z0-9-]*-[0-9a-f]{32}'
$tracked = @(git -C $project ls-files -- '*.md' '*.txt' '*.json' '*.py' '*.ps1')
foreach ($file in $tracked) {
    $path = Join-Path $project $file
    if (!(Test-Path -LiteralPath $path)) { continue }
    $text = Get-Content -Raw -LiteralPath $path
    if (!$text) { continue }
    foreach ($match in [regex]::Matches($text, $idRegex)) {
        if (!$kept.ContainsKey($match.Value)) { $kept[$match.Value] = "cited in $file" }
    }
}

# ============ kept: evidence named by kept runs (one level) ============

$names = @{}
foreach ($run in $runs) { $names[$run.Dir.Name] = $run }
foreach ($root in @($kept.Keys)) {
    if (!$names.ContainsKey($root)) { continue }
    # Campaign runners print their nested evidence in the top-level logs; deeper files are QEMU
    # traces and screenshots, slow to read and never the only reference.
    $logs = @($names[$root].Dir.EnumerateFiles('*', [IO.SearchOption]::TopDirectoryOnly) |
        Where-Object { $_.Extension -in '.log', '.json', '.stderr' -and $_.Length -lt 16MB })
    foreach ($log in $logs) {
        $text = Get-Content -Raw -LiteralPath $log.FullName
        if (!$text) { continue }
        foreach ($match in [regex]::Matches($text, $idRegex)) {
            if ($names.ContainsKey($match.Value) -and !$kept.ContainsKey($match.Value)) { $kept[$match.Value] = "named by $root" }
        }
    }
}

# ============ report and delete ============

$remove = @($runs | Where-Object { !$kept.ContainsKey($_.Dir.Name) })
# .NET enumeration: Get-ChildItem -Recurse is far too slow over hundreds of thousands of files.
$bytes = 0
foreach ($run in $remove) {
    foreach ($file in $run.Dir.EnumerateFiles('*', [IO.SearchOption]::AllDirectories)) { $bytes += $file.Length }
}
$reasons = @($kept.Values | ForEach-Object { ($_ -split ' ')[0] } | Group-Object | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ', '
"Run directories: {0}; kept {1} ({2}); to delete {3}, {4:N1} GB" -f $runs.Count, ($runs.Count - $remove.Count), $reasons, $remove.Count, ($bytes / 1GB)
$remove | Group-Object Name | Sort-Object Count -Descending | ForEach-Object { '  {0,-40} {1,4}' -f $_.Name, $_.Count }

if (!$Apply) { 'Dry run: nothing deleted. Use -Apply to delete.'; return }
foreach ($run in $remove) { Remove-Item -LiteralPath $run.Dir.FullName -Recurse -Force }
"Deleted $($remove.Count) run directories."
