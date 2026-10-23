# ============ isolated VirtIO queue diagnostic ============

$queueSources=@('kernel/drivers/video/virtio/queues/setup.c',
    'kernel/drivers/video/virtio/queues/exchange.c','kernel/drivers/video/virtio/queues/response.c',
    'arch/x86/kernel/drivers/video/virtio/queues/native.c','arch/x86/kernel/drivers/video/virtio/queues/pci.c',
    'arch/x86/kernel/drivers/video/virtio/queues/access.c',
    'test/arch/x86/kernel/drivers/video/virtio/queues/probe.c','arch/x86/mm/dma/arena.c')
foreach ($source in $queueSources) {
    $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
    $objects+=$object
}
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/queues/rust/ffi/validation.rs",'-o',"$output/vgpu-queue-policy.obj")
$objects+="$output/vgpu-queue-policy.obj"
