#include "mock.h"

// ====== bind_private ======

static int bind_private(void)
{
    return x86_vgpu_bind(&context,&window,&lapic,&inventory,capture,&mapping);
}

// ====== main ======

int main(void)
{
    for (BOOT_U32 repeat=0; repeat<100; ++repeat) {
        BOOT_U64 budget;
        X86_VGPU_MAPPING copied;
        fixture(); budget=pool.FreePages;
        CHECK(bind_private() && pool.FreePages==budget-3);
        CHECK(inventory.MmioPages==4 && inventory.Gpu.Ready==1);
        CHECK(x86_vgpu_authority(&mapping.Authority));
        context.Info.Framebuffer.Base=mapping.Authority.Plan.Physical[0]+1024;
        context.Info.Framebuffer.Capacity=4096;
        CHECK(!x86_paging_gpu_inputs(&context,&pool,&inventory));
        memset(&context.Info.Framebuffer,0,sizeof(context.Info.Framebuffer));
        copied=mapping;
        CHECK(!x86_vgpu_unbind(&copied));
        CHECK(!x86_paging_gpu_register(0,&mapping.Authority,x86_vgpu_authority));
        CHECK(x86_paging_gpu_leaf(mapping.Authority.Plan.Physical[0]|mapping.Authority.Cache|0x8000000000000007ULL,X86_GPU_VIRTUAL)==0);
        CHECK(x86_paging_gpu_leaf(mapping.Authority.Plan.Physical[0]|1,0x100000)==0);
        inventory.Gpu.Tables[0].Id++;
        CHECK(!x86_paging_gpu_complete(&inventory));
        CHECK(capture(&context,&pool,&lapic,&inventory));
        CHECK(x86_vgpu_unbind(&mapping) && pool.FreePages==budget && !mapping.Self);
        for (BOOT_U32 selected=1; selected<=7; ++selected) {
            fixture(); budget=pool.FreePages;
            if (selected==6) {
                CHECK(bind_private()); failure=6;
                CHECK(!x86_vgpu_unbind(&mapping) && mapping.Allocated==3 && pool.FreePages==budget-3);
            } else {
                failure=selected;
                CHECK(!bind_private());
                if (selected==2 || selected==4) CHECK(mapping.Allocated==3 && pool.FreePages==budget-3);
                else CHECK(!mapping.Self && pool.FreePages==budget);
            }
            failure=0; recover_borrow();
            if (mapping.Self) CHECK(x86_vgpu_unbind(&mapping));
            CHECK(pool.FreePages==budget && !x86_paging_gpu_owner() && !root_entries[X86_GPU_SLOT]);
        }
        fixture();
        {
            KERNEL_PHYSICAL_LEASE held[62]={0};
            for (BOOT_U32 lease=0; lease<62; ++lease) CHECK(kernel_physical_allocate(&pool,1,&held[lease]));
            budget=pool.FreePages;
            CHECK(!bind_private() && !mapping.Self && pool.FreePages==budget);
            for (BOOT_U32 lease=0; lease<62; ++lease) CHECK(kernel_physical_release(&pool,&held[lease]));
            CHECK(pool.FreePages==pool.TotalPages);
        }
        fixture(); memset(&context.Info.Gpu,0,sizeof(context.Info.Gpu)); budget=pool.FreePages;
        CHECK(bind_private() && !mapping.Self && pool.FreePages==budget);
        fixture(); budget=pool.FreePages;
        CHECK(bind_private());
        {
            BOOT_U64 id=mapping.Authority.Tables[1].Id;
            ++mapping.Authority.Tables[1].Id;
            CHECK(!capture(&context,&pool,&lapic,&inventory));
            CHECK(!x86_vgpu_unbind(&mapping) && mapping.Allocated &&
                pool.FreePages==budget-mapping.Allocated);
            mapping.Authority.Tables[1].Id=id;
            CHECK(x86_vgpu_unbind(&mapping) && pool.FreePages==budget);
        }
        fixture(); budget=pool.FreePages; CHECK(bind_private());
        {
            BOOT_U64 entry=root_entries[X86_GPU_SLOT];
            root_entries[X86_GPU_SLOT]=0xabc003;
            CHECK(!x86_vgpu_unbind(&mapping) && mapping.Allocated==3);
            root_entries[X86_GPU_SLOT]=entry;
            CHECK(x86_vgpu_unbind(&mapping) && pool.FreePages==budget);
        }
        fixture(); budget=pool.FreePages;
        {
            KERNEL_VGPU_PAGE_PLAN plan;
            CHECK(kernel_vgpu_plan(&context.Info.Gpu,pool.PhysicalBits,&plan));
            context.Info.Framebuffer.Base=plan.Physical[0]-4096;
            context.Info.Framebuffer.Capacity=8192;
            context.Info.Framebuffer.Used=4096;
            CHECK(!bind_private() && !mapping.Self && pool.FreePages==budget);
        }
        fixture(); budget=pool.FreePages;
        {
            BOOT_U64 physical=(pool.Free[0].Base+16383)&~16383ULL;
            for (BOOT_U32 byte=0; byte<8; ++byte) context.Info.Gpu.Config[0x20+byte]=(unsigned char)((physical|4)>>(byte*8));
            CHECK(!bind_private() && !mapping.Self && pool.FreePages==budget);
        }
    }
    printf("PASS: VirtIO mapping %u checks, 100 cycles, real allocator/leases, rollback and retry\n",checks);
    return 0;
}
