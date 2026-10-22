#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t checks, failure, views;
static X86_VGPU_MAPPING owner;
static KERNEL_BOOT_CONTEXT context;
__declspec(align(8)) static unsigned char registers[4096];
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL native:%u %s\n",(unsigned)__LINE__,#x);exit(1);} } while (0)

// ====== mocks ======

const X86_PAGING_GPU_AUTH *x86_paging_gpu_owner(void) { return failure==1?0:&owner.Authority; }
int x86_vgpu_authority(const X86_PAGING_GPU_AUTH *auth) { return failure!=2 && auth==&owner.Authority; }
int x86_vgpu_transport_end(const X86_VGPU_TRANSPORT *transport) { (void)transport; return failure!=3; }
int x86_vgpu_queue_pci(const KERNEL_VGPU_BOOT *gpu) { (void)gpu; return failure!=4; }
int x86_vgpu_transport_clock(void *opaque,uint32_t *value) { (void)opaque; *value=77; return 1; }
uint32_t kernel_vgpu_pci_probe(const unsigned char *config,uint32_t bytes,const uint64_t *sizes,
    uint32_t count,uint32_t bits,KERNEL_VGPU_PCI_LAYOUT *layout)
{
    (void)config; (void)bytes; (void)sizes; (void)count; (void)bits;
    memset(layout,0,sizeof(*layout));
    layout->Regions[0].Length=56; layout->Regions[1].Length=64; layout->Regions[1].Multiplier=4;
    layout->Regions[2].Length=1;
    return failure==5?KERNEL_VGPU_PCI_REJECTED:KERNEL_VGPU_PCI_READY;
}
volatile unsigned char *x86_vgpu_queue_view(BOOT_U64 address,uint32_t width)
{
    CHECK(address>=X86_GPU_VIRTUAL && address-X86_GPU_VIRTUAL<=4096-width);
    ++views;
    return registers+(address-X86_GPU_VIRTUAL);
}

// ====== main ======

int main(void)
{
    for (uint32_t cycle=0; cycle<100; ++cycle) {
        X86_VGPU_TRANSPORT transport={0}; X86_VGPU_QUEUE_TRANSPORT native={0}; VGPU_QUEUE_IO_CONTEXT io={0};
        memset(&owner,0,sizeof(owner)); memset(registers,0,sizeof(registers)); failure=0;
        owner.Self=(BOOT_U64)&owner; owner.Context=&context; owner.Authority.PhysicalBits=52;
        owner.Authority.Plan.Count=1; owner.Authority.Plan.Offset[1]=128; owner.Authority.Plan.Offset[2]=256;
        transport.Self=(BOOT_U64)&transport; transport.Owner=&owner; transport.Base=X86_GPU_VIRTUAL;
        transport.Clock.Mask=0xffffff;
        CHECK(x86_vgpu_queue_open(&transport,&native,&io));
        uint32_t value=15;
        CHECK(io.Access(io.Context,0,20,1,1,&value) && registers[20]==15);
        value=8; CHECK(io.Access(io.Context,0,24,2,1,&value) && registers[24]==8);
        value=0x12345678; CHECK(io.Access(io.Context,0,32,4,1,&value));
        value=0; CHECK(io.Access(io.Context,0,32,4,0,&value) && value==0x12345678);
        value=0; CHECK(io.Access(io.Context,1,3,2,1,&value) && registers[140]==0);
        CHECK(io.Access(io.Context,2,0,1,0,&value));
        CHECK(io.Clock(io.Context,&value) && value==77 && io.Barrier(io.Context));
        uint32_t before=views;
        for (uint32_t offset=0; offset<64; ++offset)
            for (uint32_t width=0; width<9; ++width) {
                if (kernel_vgpu_queue_register(offset,width,0)) continue;
                CHECK(!io.Access(io.Context,0,offset,width,0,&value));
            }
        CHECK(views==before);
        CHECK(!io.Access(io.Context,0,18,2,1,&value));
        value=16; CHECK(!io.Access(io.Context,1,16,2,1,&value));
        value=0; CHECK(!io.Access(io.Context,1,16,2,1,&value));
        CHECK(!io.Access(io.Context,1,3,2,0,&value));
        CHECK(!io.Access(io.Context,2,0,1,1,&value));
        CHECK(!io.Access(io.Context,2,1,1,0,&value));
        CHECK(!io.Access(io.Context,0,20,1,0,(uint32_t *)&native));
        CHECK(!io.Access(io.Context,0,20,1,0,(uint32_t *)&transport));
        CHECK(!io.Access(io.Context,0,20,1,0,(uint32_t *)&owner));
        value=0; CHECK(!io.Access(io.Context,0,28,2,1,&value));
        for (failure=1; failure<=2; ++failure) CHECK(!io.Access(io.Context,0,20,1,0,&value));
        failure=0; ++transport.Base; CHECK(!io.Access(io.Context,0,20,1,0,&value)); --transport.Base;
        X86_VGPU_QUEUE_TRANSPORT copy=native; CHECK(!x86_vgpu_queue_barrier(&copy));
        native.Layout.Regions[1].Multiplier=UINT32_MAX;
        CHECK(!io.Access(io.Context,1,65535,2,1,&value));
        for (failure=3; failure<=5; ++failure) {
            X86_VGPU_QUEUE_TRANSPORT rejected={0}; VGPU_QUEUE_IO_CONTEXT untouched={0};
            CHECK(!x86_vgpu_queue_open(&transport,&rejected,&untouched) && !untouched.Context && !rejected.Self);
        }
        failure=0; native.Self=0; CHECK(!io.Access(io.Context,0,20,1,0,&value));
    }
    printf("PASS %u native queue checks, MMIO widths/bounds/ownership; PCI and clock mocked\n",checks);
    return 0;
}
