# ============ plan-only toolchain paths ============

$output = Join-Path $project 'kernel/build/plan-only'
$ToolchainBin = Join-Path $project 'plan-msvc/bin/Hostx64/x64'
$sdk = @{FullName = (Join-Path $project 'plan-sdk')}

# ====== Write-PlanCommand ======

function Write-PlanCommand([string]$Program, [string[]]$Arguments) {
    $root = $project.Replace('\','/')
    $out = $output.Replace('\','/')
    $normalized = foreach ($argument in $Arguments) {
        $argument.Replace('\','/').Replace($out,'{out}').Replace($root,'{repo}')
    }
    @((Split-Path $Program -Leaf)) + @($normalized) -join "`t"
}
