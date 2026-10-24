param(
    [string]$Suite = 'all',
    [switch]$List
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$suites = [ordered]@{
    boot = 'arch/x86/boot/uefi/scripts/build.ps1'
    kernel = 'test/kernel/host.ps1'
    bootstrap = 'test/kernel/bootstrap/scripts/host.ps1'
    scheduler = 'kernel/scheduler/scripts/test.ps1'
    'x86-scheduler' = 'arch/x86/kernel/scheduler/scripts/test.ps1'
    paging = 'arch/x86/mm/paging/scripts/test.ps1'
    ps2 = 'kernel/drivers/input/ps2/scripts/test.ps1'
    framebuffer = 'kernel/drivers/video/framebuffer/scripts/test.ps1'
    'x86-framebuffer' = 'arch/x86/kernel/drivers/video/framebuffer/scripts/test.ps1'
    console = 'kernel/console/video/scripts/test.ps1'
    hpet = 'arch/x86/kernel/timer/hpet/description/scripts/test.ps1'
    watchdog = 'arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1'
    'uefi-pci' = 'arch/x86/boot/uefi/pci/scripts/test.ps1'
    'pci-ecam' = 'arch/x86/kernel/drivers/bus/pci/ecam/scripts/test.ps1'
    'pci-topology' = 'arch/x86/kernel/drivers/bus/pci/topology/scripts/test.ps1'
    'iommu-activation' = 'arch/x86/kernel/drivers/memory/iommu/intel/activation/scripts/test.ps1'
    'iommu-discovery' = 'arch/x86/kernel/drivers/memory/iommu/intel/discovery/scripts/test.ps1'
    'iommu-domains' = 'arch/x86/kernel/drivers/memory/iommu/intel/domains/scripts/test.ps1'
    'iommu-invalidation' = 'arch/x86/kernel/drivers/memory/iommu/intel/invalidation/scripts/test.ps1'
    'iommu-registers' = 'arch/x86/kernel/drivers/memory/iommu/intel/registers/scripts/test.ps1'
    'iommu-routing' = 'arch/x86/kernel/drivers/memory/iommu/intel/routing/scripts/test.ps1'
    'x86-virtio' = 'arch/x86/kernel/drivers/video/virtio/scripts/test.ps1'
    'dma-arena' = 'arch/x86/mm/dma/scripts/test.ps1'
    pointer = 'kernel/console/video/scripts/pointer/test.ps1'
    diagnostics = 'kernel/diagnostics/scripts/test.ps1'
    'ps2-mouse' = 'kernel/drivers/input/ps2/scripts/mouse/test.ps1'
    'virtio-discovery' = 'kernel/drivers/video/virtio/discovery/scripts/test.ps1'
    'virtio-initialization' = 'kernel/drivers/video/virtio/initialization/scripts/test.ps1'
    'virtio-mapping' = 'kernel/drivers/video/virtio/mapping/scripts/test.ps1'
    'virtio-queues' = 'kernel/drivers/video/virtio/queues/scripts/test.ps1'
    'virtio-resources' = 'kernel/drivers/video/virtio/resources/scripts/test.ps1'
}
# The loader build writes esp/EFI/BOOT: without -BootKernel it leaves the probe loader there, which
# does not start the kernel and breaks every later QEMU campaign. The suite keeps the boot loader.
$arguments = @{ boot = @('-BootKernel') }
# FW-012: QEMU campaigns run separately; every other test.ps1 must be listed above.
$excluded = @('test/arch/x86/kernel/drivers/memory/iommu/intel/dma/scripts/test.ps1')

# ============ resolve supported suite shell ============

function Resolve-SuiteShell {
    $runtime = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($runtime) { return $runtime.Source }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'PowerShell/7/pwsh.exe'),
        (Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'PowerShell 7 (pwsh.exe) required for host suites. Add it to PATH before running tests.'
}

# ============ isolated child process ============

function Invoke-IsolatedSuite([string]$Shell, [string]$Script, [string]$Log, [string]$ErrorLog, [string[]]$Extra) {
    $process = Start-Process -FilePath $Shell -ArgumentList (@('-NoProfile','-File',('"' + $Script + '"')) + $Extra) `
        -WorkingDirectory $project -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $Log -RedirectStandardError $ErrorLog
    try {
        $null = $process.Handle
        $process.WaitForExit()
        if ($null -eq $process.ExitCode) { throw 'Child exit code unavailable.' }
        return $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

# ============ suite inventory ============

$listed = @($suites.Values) + $excluded
# Source folders only: kernel/build holds ~20 GB of run evidence, reuse/ is reference only.
$roots = Get-ChildItem -LiteralPath $project -Directory | Where-Object { $_.Name -notin @('reuse','build','.git') }
$missing = $roots | ForEach-Object {
        if ($_.Name -eq 'kernel') {
            Get-ChildItem -LiteralPath $_.FullName -Directory | Where-Object Name -ne 'build' |
                ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter test.ps1 -File }
        }
        else { Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter test.ps1 -File }
    } |
    ForEach-Object { $_.FullName.Substring($project.Length + 1).Replace([string][char]92, '/') } |
    Where-Object { $listed -notcontains $_ }
if ($missing) { throw ('[ FAIL ] Suites not listed in run.ps1: ' + ($missing -join ', ')) }
if ($Suite -ne 'all' -and !$suites.Contains($Suite)) { throw "Unknown suite: $Suite (use -List)" }
if ($List) {
    $suites.GetEnumerator() | ForEach-Object { '{0}: {1}' -f $_.Key, $_.Value }
    return
}
$output = Join-Path $project ('kernel/build/test-run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$shell = Resolve-SuiteShell
$selected = if ($Suite -eq 'all') { @($suites.Keys) } else { @($Suite) }
$results = @()

# ============ isolated supported toolchain runners ============

foreach ($name in $selected) {
    $script = Join-Path $project $suites[$name]
    $log = Join-Path $output ($name + '.log')
    $errorLog = Join-Path $output ($name + '.stderr.log')
    Write-Output "[ INFO ] Suite $name"
    $code = Invoke-IsolatedSuite $shell $script $log $errorLog @($arguments[$name])
    $results += [pscustomobject]@{ Suite = $name; ExitCode = $code; Log = $log; ErrorLog = $errorLog }
    $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    if ($code -ne 0) {
        # FW-012: keep running the other suites; the summary reports every failure.
        Get-Content -LiteralPath $log -Tail 20
        Get-Content -LiteralPath $errorLog -Tail 20
        Write-Output "[ FAIL ] Suite $name; logs: $log, $errorLog"
        continue
    }
    Write-Output "[ OK ] Suite $name; log: $log"
}

# ============ summary ============

$failed = @($results | Where-Object { $_.ExitCode -ne 0 })
Write-Output ('Host suites: {0} passed, {1} failed; evidence {2}. QEMU campaigns are separate.' -f
    ($results.Count - $failed.Count), $failed.Count, $output)
if ($failed) { throw ('[ FAIL ] ' + (($failed | ForEach-Object Suite) -join ', ')) }
