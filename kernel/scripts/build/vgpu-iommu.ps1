# ============ V1: VirtIO GPU under VT-d translation (diagnostic only) ============

# The verified ECAM, topology, DMAR, routing, register, domain, QI and activation modules are
# linked into the VirtIO resource image without their own probes.
$bus='arch/x86/kernel/drivers/bus/pci'
$intel='arch/x86/kernel/drivers/memory/iommu/intel'
$iommuSources=@("$bus/ecam/table.c","$bus/ecam/find.c","$bus/ecam/reader.c","$bus/ecam/validation.c","$bus/ecam/backend/access.c",
    "$bus/topology/capture.c","$bus/topology/validation.c","$bus/topology/native.c",
    "$intel/discovery/table.c","$intel/discovery/find.c","$intel/routing/select.c",
    "$intel/registers/lifecycle.c","$intel/registers/preflight.c","$intel/registers/tables.c","$intel/registers/access.c",
    "$intel/registers/hardware/view.c","$intel/domains/lifecycle.c","$intel/domains/planning.c","$intel/domains/construction.c",
    "$intel/domains/verification.c","$intel/invalidation/lifecycle.c","$intel/invalidation/validation.c","$intel/invalidation/queue.c",
    "$intel/invalidation/memory.c","$intel/invalidation/backend/cpu.c","$intel/invalidation/backend/mmio.c",
    "$intel/activation/sequence.c","$intel/activation/faults.c","$intel/activation/backend/mmio.c",
    'arch/x86/kernel/drivers/video/virtio/iommu/domain.c')
# V2b: the persistent session also needs the arena, the surface mapper and its geometry policy.
if ($VgpuIommu) { $iommuSources+=@('arch/x86/kernel/drivers/video/virtio/iommu/session.c','arch/x86/mm/dma/arena.c',
    'arch/x86/kernel/drivers/video/virtio/resources/mapping.c','arch/x86/kernel/drivers/video/virtio/resources/construction.c',
    # V2c: the persistent GPU transport, queues, resource and scanout.
    'arch/x86/kernel/drivers/video/virtio/iommu/present.c','arch/x86/kernel/drivers/video/virtio/initialization/transport.c',
    'arch/x86/kernel/drivers/video/virtio/initialization/access.c','kernel/drivers/video/virtio/initialization/protocol.c',
    'kernel/drivers/video/virtio/queues/setup.c','kernel/drivers/video/virtio/queues/exchange.c','kernel/drivers/video/virtio/queues/response.c',
    'arch/x86/kernel/drivers/video/virtio/queues/native.c','arch/x86/kernel/drivers/video/virtio/queues/pci.c',
    'arch/x86/kernel/drivers/video/virtio/queues/access.c','kernel/drivers/video/virtio/resources/resource.c',
    'kernel/drivers/video/virtio/resources/scanout.c') }
foreach ($source in $iommuSources) {
    $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
    $objects+=$object
}
foreach ($policy in @(@("$bus/ecam/rust/ffi/address.rs",'v1-ecam'),@("$bus/topology/rust/ffi/graph.rs",'v1-graph'),
    @("$intel/discovery/rust/ffi/fields.rs",'v1-fields'),@("$intel/routing/rust/ffi/resolve.rs",'v1-routing'),
    @("$intel/registers/rust/ffi/policy.rs",'v1-registers'),@("$intel/domains/rust/ffi/policy.rs",'v1-domains'),
    @("$intel/invalidation/rust/ffi/policy.rs",'v1-qi'),@("$intel/activation/rust/ffi/policy.rs",'v1-activation'))) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/$($policy[0])",'-o',"$output/$($policy[1]).obj")
    $objects+="$output/$($policy[1]).obj"
}
if ($VgpuIommu) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/kernel/drivers/video/virtio/resources/rust/ffi/geometry.rs",'-o',"$output/v2b-geometry.obj")
    $objects+="$output/v2b-geometry.obj"
    foreach ($policy in @(@('kernel/drivers/video/virtio/initialization/rust/ffi/policy.rs','v2c-init'),
        @('kernel/drivers/video/virtio/queues/rust/ffi/validation.rs','v2c-queues'))) {
        Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
            '-C','opt-level=2','-C','overflow-checks=yes',"$project/$($policy[0])",'-o',"$output/$($policy[1]).obj")
        $objects+="$output/$($policy[1]).obj"
    }
}
