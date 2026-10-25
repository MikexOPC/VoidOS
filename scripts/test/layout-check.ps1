# K1 criterion 5: no kernel address written by hand outside arch/x86/configs/layout.h. Looks for the
# image window, slot shifts with a literal slot and the fixed boot windows in tracked sources. Test
# fixtures (test/), documentation, the reference tree (reuse/) and the copies checked by
# Assert-LayoutCopies are exempt.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$patterns = @(
    '0x0*100000000(ULL)?\b', '0x0*100100000(ULL)?\b', '0x1_0000_0000', '0x1_0010_0000',
    '\b\d{3}ULL\s*<<\s*39', '0x0000[4-7]0{11}', '0x0000700000[0-9a-f]{6}ULL', '0x0000_[4-7]000_[0-9a-f]{4}_[0-9a-f]{4}',
    '\(\s*\d{3}\s*<<\s*39\s*\)', '0xFFFF_F[0-9A-Fa-f]{3}_', 'root *\+ *\d{3} *\* *8', 'pml4\[\d{3}\]'
)
$exempt = '^(test/|docs/|reuse/|review_results/|arch/x86/configs/layout\.h$)'
# Rust copies compared with layout.h by Assert-LayoutCopies (scripts/lib/layout.ps1): the const line only.
$copy = 'const (KERNEL_IMAGE_BASE|GUARD|LAPIC_VIRTUAL|VIDEO): u64'
$files = @(git -C $project ls-files -- '*.c' '*.h' '*.rs' '*.asm' '*.ps1' '*.py' | Where-Object { $_ -notmatch $exempt -and $_ -ne 'scripts/test/layout-check.ps1' })
$found = @()
foreach ($file in $files) {
    $lines = Get-Content -LiteralPath (Join-Path $project $file)
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        foreach ($pattern in $patterns) {
            if ($lines[$index] -match $pattern -and $lines[$index] -notmatch $copy) { $found += "${file}:$($index + 1): $($lines[$index].Trim())"; break }
        }
    }
}
if ($found) { $found; throw "Kernel addresses written outside layout.h: $($found.Count)" }
"PASS: no kernel address outside layout.h ($($files.Count) files)"
