# ============ isolated VT-d owned mapping and capabilities ============

$module='arch/x86/kernel/drivers/memory/iommu/intel/registers'
foreach ($source in @("$module/lifecycle.c","$module/preflight.c","$module/tables.c","$module/access.c","$module/hardware/view.c",
    'test/arch/x86/kernel/drivers/memory/iommu/intel/registers/integration/probe.c')) {
    $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
    Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
    $objects+=$object
}
Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
    '-C','opt-level=2','-C','overflow-checks=yes',"$project/$module/rust/ffi/policy.rs",'-o',"$output/iommu-registers-policy.obj")
$objects+="$output/iommu-registers-policy.obj"
if ($TestIommuDomain) {
    $module='arch/x86/kernel/drivers/memory/iommu/intel/domains'
    foreach ($source in @("$module/lifecycle.c","$module/planning.c","$module/construction.c","$module/verification.c",
        'test/arch/x86/kernel/drivers/memory/iommu/intel/domains/integration/probe.c')) {
        $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
        $objects+=$object
    }
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/$module/rust/ffi/policy.rs",'-o',"$output/iommu-domain-policy.obj")
    $objects+="$output/iommu-domain-policy.obj"
}

if ($TestIommuQi) {
    $module='arch/x86/kernel/drivers/memory/iommu/intel/invalidation'
    foreach ($source in @("$module/lifecycle.c","$module/validation.c","$module/queue.c","$module/memory.c","$module/backend/cpu.c","$module/backend/mmio.c",
        'test/arch/x86/kernel/drivers/memory/iommu/intel/invalidation/integration/probe.c')) {
        $object=Join-Path $output ($source.Replace('/','-').Replace('.c','.obj'))
        Invoke-Compiler (Join-Path $ToolchainBin 'cl.exe') ($flags+@(('/Fo'+$object),(Join-Path $project $source)))
        $objects+=$object
    }
    Invoke-Compiler $rustc @('--edition=2024','--crate-type=lib','--emit=obj','-D','warnings','-C','panic=abort',
        '-C','opt-level=2','-C','overflow-checks=yes',"$project/$module/rust/ffi/policy.rs",'-o',"$output/iommu-qi-policy.obj")
    $objects+="$output/iommu-qi-policy.obj"
}
