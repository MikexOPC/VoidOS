#include "mock.h"

// ====== main ======

int main(void)
{
    for (unsigned count=1; count<=16; ++count) {
        domain_fixture(count,1); BOOT_U64 budget=pool.FreePages;
        CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain));
        CHECK(domain.State==3 && domain.TableCount==6 && x86_vtd_domain_verify(&domain));
        domain_walk_check();
        BOOT_U64 *leaf=(BOOT_U64 *)domain.Tables[5].Base,original=leaf[1];
        leaf[1]|=1ULL<<63; CHECK(!x86_vtd_domain_verify(&domain)); leaf[1]=original;
        leaf[0]=original; CHECK(!x86_vtd_domain_verify(&domain)); leaf[0]=0;
        domain.Iova[0]+=4096; CHECK(!x86_vtd_domain_verify(&domain)); domain.Iova[0]-=4096;
        CHECK(!x86_vtd_domain_pages(&domain,1));
        KERNEL_PHYSICAL_LEASE table_saved=domain.Tables[0];
        domain.Tables[0]=request.Buffers[0].Lease;
        CHECK(!x86_vtd_domain_verify(&domain) && !x86_vtd_domain_release(&domain));
        CHECK(kernel_physical_is_pinned(&pool,&request.Buffers[0].Lease)); domain.Tables[0]=table_saved;
        CHECK(kernel_physical_pin(&pool,&request.Buffers[0].Lease,0));
        CHECK(!x86_vtd_domain_verify(&domain) && !x86_vtd_domain_release(&domain));
        CHECK(kernel_physical_pin(&pool,&request.Buffers[0].Lease,1));
        inventory_fail=1; CHECK(!x86_vtd_domain_release(&domain) && domain.Allocated==6); inventory_fail=0;
        CHECK(x86_vtd_domain_release(&domain) && pool.FreePages==budget && !domain.Self);
        domain_buffers_release();
    }
    // Revocation: only the revoked buffer's leaves become zero, the rest stays mapped and verified.
    domain_fixture(3,2); BOOT_U64 revoke_budget=pool.FreePages;
    CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain) && !domain.Revoked);
    BOOT_U64 *leaves=(BOOT_U64 *)domain.Tables[5].Base;
    BOOT_U32 slots[3]; for (unsigned index=0; index<3; ++index) slots[index]=(BOOT_U32)((domain.Iova[index]-X86_VTD_DMA_BASE)/4096);
    CHECK(leaves[slots[1]] && leaves[slots[1]+1]);
    CHECK(!x86_vtd_domain_revoke(&domain,3) && !x86_vtd_domain_revoke(&domain,16) && domain.State==3);
    CHECK(x86_vtd_domain_revoke(&domain,1) && domain.Revoked==2 && domain.State==3 && x86_vtd_domain_verify(&domain));
    CHECK(!leaves[slots[1]] && !leaves[slots[1]+1] && leaves[slots[0]] && leaves[slots[2]] && leaves[slots[2]+1]);
    CHECK(!x86_vtd_domain_revoke(&domain,1) && domain.Revoked==2);
    domain_walk_check();
    leaves[slots[1]]=leaves[slots[0]]; CHECK(!x86_vtd_domain_verify(&domain) && !x86_vtd_domain_revoke(&domain,0)); leaves[slots[1]]=0;
    domain.Revoked=8; CHECK(!x86_vtd_domain_verify(&domain)); domain.Revoked=2;
    borrow_count=0; borrow_fail=domain.TableCount+1;
    CHECK(!x86_vtd_domain_revoke(&domain,0) && domain.State==1 && domain.Revoked==3); borrow_fail=0;
    domain.State=3; domain.Revoked=2; CHECK(x86_vtd_domain_verify(&domain));
    CHECK(x86_vtd_domain_release(&domain) && pool.FreePages==revoke_budget); domain_buffers_release();
    domain_fixture(1,4100);
    CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain) && domain.TableCount==14);
    domain_walk_check(); CHECK(x86_vtd_domain_release(&domain)); domain_buffers_release();
    domain_fixture(16,256);
    CHECK(kernel_physical_pin(&pool,&request.Buffers[15].Lease,0));
    CHECK(kernel_physical_release(&pool,&request.Buffers[15].Lease));
    CHECK(kernel_physical_allocate(&pool,260,&request.Buffers[15].Lease));
    CHECK(kernel_physical_pin(&pool,&request.Buffers[15].Lease,1));
    CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain) && domain.TableCount==14);
    domain_walk_check(); CHECK(x86_vtd_domain_release(&domain)); domain_buffers_release();
    domain_fixture(1,4101);
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain) && !domain.Self);
    domain_buffers_release();
    for (unsigned stage=1; stage<=28; ++stage) {
        domain_fixture(1,4100); BOOT_U64 budget=pool.FreePages; borrow_fail=stage;
        CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain));
        CHECK(!domain.Self && pool.FreePages==budget); borrow_fail=0; domain_buffers_release();
    }
    for (unsigned stage=1; stage<=14; ++stage) {
        domain_fixture(1,4100); BOOT_U64 budget=pool.FreePages; pin_fail=stage;
        CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain));
        CHECK(!domain.Self && pool.FreePages==budget); pin_fail=0; domain_buffers_release();
    }
    for (unsigned available=0; available<14; ++available) {
        domain_fixture(1,4100); KERNEL_PHYSICAL_LEASE busy[63]={0}; unsigned used=63-available;
        for (unsigned index=0; index<used; ++index) CHECK(kernel_physical_allocate(&pool,1,&busy[index]));
        BOOT_U64 budget=pool.FreePages;
        CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain) && !domain.Self && pool.FreePages==budget);
        for (unsigned index=0; index<used; ++index) CHECK(kernel_physical_release(&pool,&busy[index]));
        domain_buffers_release();
    }
    domain_fixture(1,1); request.Buffers[1]=request.Buffers[0];
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Buffers[1]=(X86_VTD_BUFFER){0};
    request.Count=2; request.Buffers[1]=request.Buffers[0];
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Count=1; request.Buffers[1]=(X86_VTD_BUFFER){0};
    request.Buffers[0].Access=2; CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Buffers[0].Access=1;
    request.Segment=1; CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Segment=0;
    request.Count=17; CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Count=1;
    request.Function=8; CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); request.Function=7;
    inventory.Tables[0].Physical=request.Buffers[0].Lease.Base;
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); inventory.Tables[0].Physical=0x400000;
    context.Info.Framebuffer.Base=request.Buffers[0].Lease.Base; context.Info.Framebuffer.Capacity=4096;
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain)); context.Info.Framebuffer.Capacity=0;
    CHECK(!x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,(X86_VTD_DOMAIN *)&request));
    domain_buffers_release();
    printf("PASS %u domain checks; real allocator, 16-range/max4100-page walks and all allocation/pin/borrow failures\n",checks);
    return 0;
}
