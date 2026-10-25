param([string]$OutputDirectory = 'D:/OS/kernel/build/vgpu-present-host')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../../../../../../../..").Path
. (Join-Path $project 'scripts/lib/common.ps1')
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null

# ============ G6 VirtIO present object ============

$saved = Use-Msvc
try {
    $sources = @('arch/x86/kernel/drivers/video/virtio/iommu/present/lifecycle.c',
        'arch/x86/kernel/drivers/video/virtio/iommu/present/update.c',
        'kernel/time/clock.c', 'lib/geometry/damage.c', 'lib/geometry/rect.c',
        'test/arch/x86/kernel/drivers/video/virtio/iommu/present_test.c') | ForEach-Object { "$project/$_" }
    $flags = @('/nologo', '/TC', '/std:c11', '/W4', '/WX', '/Zi', '/fsanitize=address', '/MD', '/O2', "/I$project")
    Invoke-Checked "$($saved.Bin)/cl.exe" ($flags + $sources + @("/Fo$out/", "/Fd$out/host.pdb", "/Fe$out/host.exe", '/link', '/incremental:no'))
    Invoke-Checked "$out/host.exe" @()
    Invoke-Cppcheck $project $out $sources
} finally {
    Restore-Msvc $saved
}
"PASS: VirtIO present host evidence $out"
