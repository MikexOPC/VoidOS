# ============ isolated Intel VT-d discovery policy ============

if ($TestIommuDiscovery) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/arch/x86/kernel/drivers/memory/iommu/intel/discovery/rust/ffi/fields.rs",
        '-o',"$output/iommu-fields.obj")
    $objects+="$output/iommu-fields.obj"
}
if ($TestPciEcam) {
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/arch/x86/kernel/drivers/bus/pci/ecam/rust/ffi/address.rs",
        '-o',"$output/ecam-policy.obj")
    $objects+="$output/ecam-policy.obj"
}
if ($TestPciTopology) {
    $module='arch/x86/kernel/drivers/bus/pci/topology'
    foreach ($source in @("$module/capture.c","$module/validation.c","$module/native.c",'test/arch/x86/kernel/drivers/bus/pci/topology/integration/probe.c')) {
        $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
        $objects+=$object
    }
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/$module/rust/ffi/graph.rs",'-o',"$output/pci-graph.obj")
    $objects+="$output/pci-graph.obj"
}
if ($TestIommuRouting) {
    foreach ($source in @('arch/x86/kernel/drivers/memory/iommu/intel/routing/select.c',
        'arch/x86/kernel/drivers/memory/iommu/intel/discovery/table.c','arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.c',
        'test/arch/x86/kernel/drivers/memory/iommu/intel/routing/integration/probe.c',
        'test/arch/x86/kernel/drivers/memory/iommu/intel/routing/integration/matrix.c')) {
        $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
        $objects+=$object
    }
    foreach ($policy in @(@('discovery/rust/ffi/fields.rs','routing-fields'),@('routing/rust/ffi/resolve.rs','routing-policy'))) {
        Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
            '-C','opt-level=2','-C','overflow-checks=yes',"$project/arch/x86/kernel/drivers/memory/iommu/intel/$($policy[0])",'-o',"$output/$($policy[1]).obj")
        $objects+="$output/$($policy[1]).obj"
    }
}
if ($TestIommuActivation) {
    # Activation reuses the verified registers, domain and QI modules inside the routing image.
    $intel='arch/x86/kernel/drivers/memory/iommu/intel'
    foreach ($source in @("$intel/registers/lifecycle.c","$intel/registers/preflight.c","$intel/registers/tables.c","$intel/registers/access.c",
        "$intel/registers/hardware/view.c","$intel/domains/lifecycle.c","$intel/domains/planning.c","$intel/domains/construction.c",
        "$intel/domains/verification.c","$intel/invalidation/lifecycle.c","$intel/invalidation/validation.c","$intel/invalidation/queue.c",
        "$intel/invalidation/memory.c","$intel/invalidation/backend/cpu.c","$intel/invalidation/backend/mmio.c",
        "$intel/activation/sequence.c","$intel/activation/faults.c","$intel/activation/backend/mmio.c",
        "test/$intel/activation/integration/probe.c")) {
        $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
        $objects+=$object
    }
    if ($TestIommuDma) {
        # Block 3: test-only edu requester and the P1-P5 DMA phases.
        foreach ($source in @("test/$intel/dma/edu.c","test/$intel/dma/integration/probe.c","test/$intel/dma/integration/phases.c")) {
            $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
            Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
            $objects+=$object
        }
    }
    foreach ($policy in @('registers','domains','invalidation','activation')) {
        Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
            '-C','opt-level=2','-C','overflow-checks=yes',"$project/$intel/$policy/rust/ffi/policy.rs",'-o',"$output/activation-$policy.obj")
        $objects+="$output/activation-$policy.obj"
    }
}
