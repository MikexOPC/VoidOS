#include "mock.h"

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<100; ++cycle) for (unsigned units=1; units<=8; ++units) {
        vtd_fixture(units); BOOT_U64 budget=pool.FreePages;
        CHECK(x86_vtd_mapping_bind(&context,&window,0,&description,&inventory,&mapping));
        CHECK(pool.FreePages==budget-3 && mapping.Pinned==7 && x86_vtd_mapping_check(&mapping.Authority));
        CHECK(x86_iommu_complete(&inventory.Iommu,7,units));
        CHECK(!x86_iommu_complete(&inventory.Iommu,3,units));
        CHECK(!x86_iommu_inputs(&context,&pool,(BOOT_U64)&inventory,sizeof(inventory),(X86_IOMMU_SNAPSHOT *)&mapping));
        CHECK(mapping.Self==(BOOT_U64)&mapping && x86_vtd_mapping_check(&mapping.Authority));
        for (unsigned index=0; index<units; ++index) {
            X86_VTD_CAPABILITIES output;
            CHECK(x86_vtd_capabilities_read(&mapping,index,&output) && output.Version==0x10 && output.Compatible==1 &&
                output.AddressBits==48 && output.Domains==65536 && output.IotlbOffset==0xf0 && output.FaultOffset==0x220 && output.FaultCount==1);
            BOOT_U64 entry=mapping.Authority.Base[index]|8|0x8000000000000001ULL;
            CHECK(x86_iommu_leaf(entry,X86_IOMMU_VIRTUAL+index*8192ULL)==7);
            CHECK(!x86_iommu_leaf(entry|2,X86_IOMMU_VIRTUAL+index*8192ULL));
            CHECK(!x86_iommu_leaf(entry,X86_IOMMU_VIRTUAL+index*8192ULL+4096));
            CHECK(!x86_iommu_leaf(entry,0x100000));
        }
        X86_VTD_CAPABILITIES saved,output;
        memset(&output,0x5a,sizeof(output)); saved=output;
        registers[0][0]=0; CHECK(!x86_vtd_capabilities_read(&mapping,0,&output) && !memcmp(&saved,&output,sizeof(output))); registers[0][0]=0x10;
        CHECK(!x86_vtd_capabilities_read(&mapping,units,&output));
        CHECK(kernel_physical_pin(&pool,&mapping.Authority.Tables[0],0));
        CHECK(!x86_vtd_capabilities_read(&mapping,0,&output));
        CHECK(kernel_physical_pin(&pool,&mapping.Authority.Tables[0],1));
        BOOT_U64 *leaves=(BOOT_U64 *)mapping.Authority.Tables[2].Base,original=leaves[1];
        leaves[1]|=2; CHECK(!x86_vtd_capabilities_read(&mapping,0,&output)); leaves[1]=original;
        leaves[2]=original; CHECK(!x86_vtd_capabilities_read(&mapping,0,&output)); leaves[2]=0;
        registers[0][3]=1ULL<<63;
        CHECK(x86_vtd_capabilities_read(&mapping,0,&output) && !output.Compatible && output.Status==(1U<<31));
        registers[0][3]=0;
        failure=5; CHECK(!x86_vtd_mapping_unbind(&mapping) && mapping.Pinned==7); failure=0;
        CHECK(x86_vtd_mapping_unbind(&mapping) && pool.FreePages==budget && !roots[228] && !x86_iommu_owner());
    }
    for (unsigned fail=1; fail<=4; ++fail) {
        vtd_fixture(1); BOOT_U64 budget=pool.FreePages; failure=fail;
        CHECK(!x86_vtd_mapping_bind(&context,&window,0,&description,&inventory,&mapping));
        failure=0;
        if (mapping.Self) CHECK(x86_vtd_mapping_unbind(&mapping));
        CHECK(pool.FreePages==budget && !x86_iommu_owner() && !roots[228]);
    }
    vtd_fixture(1); description.Unit[0].Base=(BOOT_U64)arena;
    CHECK(!x86_vtd_mapping_bind(&context,&window,0,&description,&inventory,&mapping) && !mapping.Self);
    vtd_fixture(1); roots[228]=0x300003;
    CHECK(!x86_vtd_mapping_bind(&context,&window,0,&description,&inventory,&mapping) && roots[228]==0x300003);
    vtd_fixture(1); KERNEL_PHYSICAL_LEASE leases[62]={0};
    for (unsigned index=0; index<62; ++index) CHECK(kernel_physical_allocate(&pool,1,&leases[index]));
    BOOT_U64 budget=pool.FreePages;
    CHECK(!x86_vtd_mapping_bind(&context,&window,0,&description,&inventory,&mapping) && pool.FreePages==budget && !mapping.Self);
    for (unsigned index=0; index<62; ++index) CHECK(kernel_physical_release(&pool,&leases[index]));
    printf("PASS %u VT-d checks, 800 real allocator lifecycles; CPU/MMU/MMIO mocked, rollback/retry verified\n",checks);
    return 0;
}
