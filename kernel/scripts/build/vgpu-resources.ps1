# ============ isolated full-size VirtIO resource diagnostic ============

$resourceSources=@('kernel/drivers/video/virtio/resources/resource.c',
    'arch/x86/kernel/drivers/video/virtio/resources/mapping.c','arch/x86/kernel/drivers/video/virtio/resources/construction.c',
    'test/arch/x86/kernel/drivers/video/virtio/resources/probe.c')
if ($TestVgpuScanout) { $resourceSources+=@('kernel/drivers/video/virtio/resources/scanout.c',
    'test/arch/x86/kernel/drivers/video/virtio/resources/presentation/visual.c') }
foreach ($source in $resourceSources) {
    $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
    $objects+=$object
}
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/resources/rust/ffi/geometry.rs",'-o',"$output/vgpu-resources-policy.obj")
$objects+="$output/vgpu-resources-policy.obj"
