#include "kernel/drivers/video/virtio/mapping/plan.h"
#include "test/arch/x86/boot/uefi/pci/fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#value); exit(1); } } while (0)

// ====== main ======

int main(void)
{
    KERNEL_VGPU_BOOT boot;
    KERNEL_VGPU_PAGE_PLAN output, saved;
    KERNEL_VGPU_RANGES ranges={{0},{0}};
    test_vgpu_fixture(&boot);
    CHECK(kernel_vgpu_plan(&boot,48,&output));
    CHECK(output.Valid == 1 && output.Count == 4);
    for (uint32_t page=0; page<16; ++page)
        CHECK(output.Physical[page] == (page<4 ? UINT64_C(0x100000000)+page*4096 : 0));
    for (uint32_t region=0; region<4; ++region) CHECK(output.Offset[region] == region*4096);
    saved=output;
    boot.Reserved=1;
    CHECK(!kernel_vgpu_plan(&boot,48,&output) && !memcmp(&output,&saved,sizeof(output)));
    CHECK(!kernel_vgpu_plan(NULL,48,&output));
    CHECK(!kernel_vgpu_plan(&boot,48,NULL));
    CHECK(!kernel_vgpu_plan(&boot,48,(KERNEL_VGPU_PAGE_PLAN *)&boot));
    memset(&boot,0,sizeof(boot));
    CHECK(kernel_vgpu_plan(&boot,48,&output) && output.Valid == 1 && !output.Count);
    for (uint32_t offset=0; offset<4096; ++offset) {
        KERNEL_VGPU_PAGE_PLAN plan;
        ranges.Address[0]=UINT64_C(0x100001000)+offset;
        ranges.Length[0]=56;
        ranges.Address[1]=UINT64_C(0x100003000); ranges.Length[1]=4096;
        ranges.Address[2]=UINT64_C(0x100001800); ranges.Length[2]=1;
        ranges.Address[3]=UINT64_C(0x100002000); ranges.Length[3]=16;
        plan=kernel_vgpu_pages(ranges,48);
        CHECK(plan.Valid == 1 && plan.Count == 3);
        CHECK(plan.Offset[0] == offset && plan.Offset[1] == 8192);
        CHECK(plan.Physical[0] == UINT64_C(0x100001000) && plan.Physical[2] == UINT64_C(0x100003000));
    }
    printf("PASS: VirtIO page-plan C/Rust ABI %u checks\n",checks);
    return 0;
}
