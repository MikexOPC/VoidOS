#include "model.h"

// ====== main ======

int main(void)
{
    X86_PM_CLOCK clock={0,0,0x408,0xffffff,1};
    for (unsigned cycle=0; cycle<10; ++cycle) {
        qi_fixture(); latched_mode=cycle&1; BOOT_U64 budget=pool.FreePages;
        CHECK(x86_qi_prepare(&mapping,&clock,52,&qi) && pool.FreePages==budget-2);
        inventory.Iommu.Writable=0; CHECK(!x86_vtd_inventory_verify(&mapping)); inventory.Iommu.Writable=1;
        mapping.Authority.Writable=0; CHECK(!x86_vtd_inventory_verify(&mapping)); mapping.Authority.Writable=1;
        CHECK(!x86_vtd_mapping_unbind(&mapping));
        CHECK(!x86_qi_memory(&qi,2,&qi.Token));
        qi.State=4; CHECK(!x86_qi_valid(&qi)); qi.State=1;
        if (!cycle) {
            X86_VTD_DOMAIN private_domain={0}; X86_VTD_DOMAIN_REQUEST request={0};
            request.Count=1; request.PhysicalBits=52; request.Buffers[0].Access=1;
            CHECK(kernel_physical_allocate(&pool,1,&request.Buffers[0].Lease));
            CHECK(kernel_physical_pin(&pool,&request.Buffers[0].Lease,1));
            CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&private_domain));
            CHECK(x86_qi_clean_domain(&private_domain));
            CHECK(x86_vtd_domain_release(&private_domain));
            CHECK(kernel_physical_pin(&pool,&request.Buffers[0].Lease,0));
            CHECK(kernel_physical_release(&pool,&request.Buffers[0].Lease));
        }
        CHECK(x86_qi_start(&qi));
        for (unsigned request=0; request<256; ++request) {
            CHECK(x86_qi_submit(&qi)); CHECK(!x86_qi_submit(&qi)); CHECK(x86_qi_finish(&qi));
        }
        CHECK(completions==256 && qi.Token==256 && qi.Tail==0 && cleans==128U+256U*128U+(cycle?0U:384U));
        qi_close();
    }
    qi_fixture(); stall_clock=1; BOOT_U64 budget=pool.FreePages;
    CHECK(!x86_qi_prepare(&mapping,&clock,52,&qi) && !qi.Self && pool.FreePages==budget && !writes);
    CHECK(x86_vtd_mapping_unbind(&mapping));
    qi_fixture(); device[0x90/8]=0x1234001;
    CHECK(!x86_qi_prepare(&mapping,&clock,52,&qi) && !qi.Self && !writes && device[0x90/8]==0x1234001);
    CHECK(x86_vtd_mapping_unbind(&mapping));
    qi_fixture(); registers[0][2]&=~2ULL;
    CHECK(!x86_qi_prepare(&mapping,&clock,52,&qi) && !qi.Self && !writes);
    CHECK(x86_vtd_mapping_unbind(&mapping));
    qi_fixture(); CHECK(x86_qi_prepare(&mapping,&clock,52,&qi)); stall_enable=1;
    CHECK(!x86_qi_start(&qi) && !x86_qi_stop(&qi) && qi.Pins==3 && mapping.Consumer);
    stall_enable=0; qi_enable_ack(); qi_close();
    qi_fixture(); CHECK(x86_qi_prepare(&mapping,&clock,52,&qi) && x86_qi_start(&qi)); stall_completion=1;
    CHECK(x86_qi_submit(&qi) && !x86_qi_finish(&qi) && !x86_qi_stop(&qi) && qi.Pins==3);
    stall_completion=0; CHECK(x86_qi_write(&qi,0x88,8,qi.Tail)); CHECK(x86_qi_finish(&qi));
    stall_disable=1; CHECK(!x86_qi_stop(&qi) && qi.EnableIssued && qi.Pins==3);
    stall_disable=0; device[0x18/8]=0; device[0x80/8]=0; qi_close();
    qi_fixture(); CHECK(x86_qi_prepare(&mapping,&clock,52,&qi) && x86_qi_start(&qi));
    CHECK(x86_qi_submit(&qi)); *(BOOT_U32 *)qi.Pages[1].Base=0xffffffff;
    CHECK(!x86_qi_finish(&qi) && qi.Pending && qi.Pins==3);
    *(BOOT_U32 *)qi.Pages[1].Base=qi.Token; CHECK(x86_qi_finish(&qi)); qi_close();
    for (unsigned available=0; available<2; ++available) {
        qi_fixture(); KERNEL_PHYSICAL_LEASE busy[61]={0}; unsigned count=61-available;
        for (unsigned index=0; index<count; ++index) CHECK(kernel_physical_allocate(&pool,1,&busy[index]));
        budget=pool.FreePages;
        CHECK(!x86_qi_prepare(&mapping,&clock,52,&qi) && !qi.Self && pool.FreePages==budget);
        for (unsigned index=0; index<count; ++index) CHECK(kernel_physical_release(&pool,&busy[index]));
        CHECK(x86_vtd_mapping_unbind(&mapping));
    }
    for (unsigned pin=1; pin<=2; ++pin) {
        qi_fixture(); budget=pool.FreePages; pin_at=pin;
        CHECK(!x86_qi_prepare(&mapping,&clock,52,&qi) && !qi.Self && pool.FreePages==budget);
        pin_at=0; CHECK(x86_vtd_mapping_unbind(&mapping));
    }
    qi_fixture(); device[0x90/8]=0x1234000;
    CHECK(x86_qi_prepare(&mapping,&clock,52,&qi) && !writes);
    CHECK(x86_qi_start(&qi) && x86_qi_submit(&qi) && x86_qi_finish(&qi)); qi_close();
    qi_fixture(); CHECK(x86_qi_prepare(&mapping,&clock,52,&qi) && x86_qi_start(&qi) && x86_qi_submit(&qi));
    errors=1; CHECK(!x86_qi_finish(&qi) && !x86_qi_stop(&qi) && qi.Pins==3);
    errors=0; CHECK(x86_qi_finish(&qi)); qi_close();
    printf("PASS %u QI checks; 2560 completions, wrap, stale status, clock/enable/disable timeouts, retention and retry\n",checks);
    return 0;
}
