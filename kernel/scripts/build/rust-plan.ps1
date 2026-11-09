# ====== Resolve-RustPlanArgument ======

function Resolve-RustPlanArgument([string]$Argument, [hashtable]$Paths) {
    $pattern = '\{repo\}/plan-msvc/bin/Hostx64/x64|\{repo\}/plan-sdk|\{repo\}|\{out\}'
    [regex]::Replace($Argument, $pattern, { param($match) $Paths[$match.Value] })
}

# ====== prepare isolated build ======

$ToolchainBin = (Resolve-Path -LiteralPath $ToolchainBin).Path
$rustc = Join-Path $env:USERPROFILE '.cargo/bin/rustc.exe'
$nasm = & (Join-Path $project 'arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1')
$tools = @{
    'cl.exe' = Join-Path $ToolchainBin 'cl.exe'
    'ml64.exe' = Join-Path $ToolchainBin 'ml64.exe'
    'link.exe' = Join-Path $ToolchainBin 'link.exe'
    'nasm.exe' = $nasm.Nasm
    'rustc.exe' = $rustc
}
$paths = @{
    '{repo}/plan-msvc/bin/Hostx64/x64' = $ToolchainBin.Replace('\','/')
    '{repo}/plan-sdk' = $sdk.FullName.Replace('\','/')
    '{repo}' = $project.Replace('\','/')
    '{out}' = $OutputDirectory.Replace('\','/')
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$planner = Join-Path $OutputDirectory 'build-plan.exe'
$source = Join-Path $project 'scripts/tooling/build-plan/rust/plan/main.rs'
Invoke-Compiler $rustc @('--edition=2024','-D','warnings',$source,'-o',$planner)
$profileArguments = @()
if ($TestSchedulerPeriodic) { $profileArguments += '-TestSchedulerPeriodic' }
if ($TestApService -eq 'ap-timer') { $profileArguments += @('-TestApService','ap-timer') }
if ($TestK6Process) { $profileArguments += '-TestK6Process' }
$plan = @(& $planner @profileArguments)
if ($LASTEXITCODE -ne 0) { throw 'Rust build planning failed.' }

# ====== validate and execute plan ======

$convert = "convert-image.ps1`t-Image`t{out}/kernel.exe`t-Output`t{out}/kernel.elf"
if ($plan.Count -lt 3 -or $plan[-1] -cne $convert -or !$plan[-2].StartsWith("link.exe`t")) {
    throw 'Rust plan is missing the final link or conversion.'
}
$commands = for ($index = 0; $index -lt $plan.Count - 1; ++$index) {
    $fields = $plan[$index].Split([char]9)
    $tool = $fields[0]
    if ($fields.Length -lt 2 -or !$tools.ContainsKey($tool) -or
        ($tool -eq 'link.exe' -and $index -ne $plan.Count - 2)) {
        throw "Unexpected tool in Rust plan at line $($index + 1)."
    }
    $arguments = foreach ($argument in $fields[1..($fields.Length - 1)]) {
        Resolve-RustPlanArgument $argument $paths
    }
    [pscustomobject]@{
        Tool = $tool
        Arguments = $arguments
        ApTemplate = $tool -eq 'nasm.exe' -and $fields -ccontains '{out}/ap-template.bin'
    }
}
foreach ($command in $commands) {
    if ($command.Tool -eq 'link.exe') {
        $response = Join-Path $OutputDirectory 'kernel.link.rsp'
        $lines = @($command.Arguments | ForEach-Object { '"' + $_ + '"' })
        [IO.File]::WriteAllLines($response, [string[]]$lines, [Text.UTF8Encoding]::new($true))
        Invoke-Compiler $tools[$command.Tool] @('@' + $response)
    } else {
        Invoke-Compiler $tools[$command.Tool] $command.Arguments
        if ($command.ApTemplate) { Assert-ApTemplateSize (Join-Path $OutputDirectory 'ap-template.bin') }
    }
}

$elf = Join-Path $OutputDirectory 'kernel.elf'
$pending = Join-Path $OutputDirectory 'kernel.elf.pending'
& (Join-Path $project 'kernel/scripts/convert-image.ps1') -Image (Join-Path $OutputDirectory 'kernel.exe') -Output $pending
[IO.File]::Move($pending, $elf)
Write-BootStatus OK "Kernel ELF verificato e generato dal piano Rust: $elf"
