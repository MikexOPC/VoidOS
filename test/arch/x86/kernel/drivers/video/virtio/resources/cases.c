#include "mock.h"

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<100; ++cycle) {
        surface_fixture();
        CHECK(x86_vgpu_surface_bind(&gpu,1280,800,&mapping));
        CHECK(mapping.Authority.Data.Pages==1000 && mapping.Authority.Count==4 && pool.FreePages==196);
        CHECK(kernel_physical_is_pinned(&pool,&mapping.Authority.Data));
        CHECK(!kernel_physical_release(&pool,&mapping.Authority.Data));
        CHECK(x86_vgpu_surface_pixels(&mapping)==(volatile uint32_t *)X86_SURFACE_VIRTUAL);
        CHECK(!x86_surface_complete(0,0,0));
        CHECK(!x86_surface_inputs(&pool,0x400000,0,0,0));
        CHECK(!x86_surface_record(0,0,0,0,0));
        X86_SURFACE_SNAPSHOT snapshot=inventory.Surface;
        CHECK(x86_surface_complete(&snapshot,15,1000));
        ++snapshot.DataId; CHECK(!x86_surface_complete(&snapshot,15,1000));
        BOOT_U32 count=mapping.Authority.Count; mapping.Authority.Count=32;
        CHECK(!x86_surface_complete(&snapshot,15,1000)); mapping.Authority.Count=count;
        CHECK(kernel_physical_pin(&pool,&mapping.Authority.Data,0));
        CHECK(!x86_surface_inputs(&pool,0x400000,(BOOT_U64)&snapshot,sizeof(snapshot),&snapshot));
        CHECK(kernel_physical_pin(&pool,&mapping.Authority.Data,1));
        CHECK(x86_surface_leaf(mapping.Authority.Data.Base|0x8000000000000003ULL,X86_SURFACE_VIRTUAL)==6);
        CHECK(!x86_surface_leaf(mapping.Authority.Data.Base|0x8000000000000003ULL,0x5000));
        CHECK(!x86_surface_leaf(mapping.Authority.Data.Base|3,X86_SURFACE_VIRTUAL));
        CHECK(!x86_surface_leaf(mapping.Authority.Data.Base|0x8000000000000003ULL,X86_SURFACE_VIRTUAL-4096));
        CHECK(!x86_surface_leaf((mapping.Authority.Data.Base+1000*4096)|0x8000000000000003ULL,X86_SURFACE_VIRTUAL+1000*4096));
        BOOT_U64 actual=roots[227]; roots[227]=0xdead0003;
        CHECK(!x86_vgpu_surface_unbind(&mapping) && pool.FreePages==196);
        roots[227]=actual; CHECK(x86_vgpu_surface_unbind(&mapping));
        CHECK(pool.FreePages==1200 && !roots[227] && !x86_surface_owner());
        for (failure=1; failure<=3; ++failure) {
            unsigned scenario=failure; CHECK(!x86_vgpu_surface_bind(&gpu,1280,800,&mapping));
            if (scenario==1) {
                CHECK(pool.FreePages<1200 && mapping.DataPinned);
                failure=0; CHECK(x86_vgpu_surface_unbind(&mapping)); failure=scenario;
            }
            CHECK(pool.FreePages==1200 && !roots[227] && !x86_surface_owner());
        }
        failure=0;
        CHECK(!x86_vgpu_surface_bind(&gpu,4096,4096,&mapping) && pool.FreePages==1200);
        roots[227]=3; CHECK(!x86_vgpu_surface_bind(&gpu,1280,800,&mapping)); roots[227]=0;
        CHECK(!x86_vgpu_surface_bind(&gpu,1280,800,(X86_SURFACE_MAPPING *)&pool));
    }
    printf("PASS %u surface mapper checks, 100 real allocator cycles, CPU/MMU mocked\n",checks);
    return 0;
}
