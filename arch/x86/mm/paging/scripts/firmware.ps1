param([string]$KernelDirectory, [ValidateRange(10,20)][int]$Boots = 10)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
. (Join-Path $project 'scripts/lib/common.ps1')
if (!$KernelDirectory) {
    $KernelDirectory = Join-Path $project ('kernel/build/paging-kernel-' + [guid]::NewGuid().ToString('N'))
    & "$project/kernel/scripts/build.ps1" -OutputDirectory $KernelDirectory
}
$python = Resolve-OsPython
$image = "$project/arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"
$provider = "$project/kernel/drivers/video/framebuffer/scripts/firmware.py"

# ============ final ordinary inventory regression ============

foreach ($absent in @($false, $true)) {
    $arguments = @($provider,'--image',$image,'--kernel',"$KernelDirectory/kernel.elf",
        '--mapping-plan','--boots',[string]$Boots)
    if ($absent) { $arguments += '--absent' }
    & $python @arguments
    if ($LASTEXITCODE) { throw 'Owned paging firmware regression failed' }
}
"PASS: ordinary inventory with video metadata and without display; live video mapping not exercised"
