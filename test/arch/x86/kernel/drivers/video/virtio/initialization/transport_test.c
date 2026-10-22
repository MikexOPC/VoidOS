#include "arch/x86/kernel/drivers/video/virtio/initialization/transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t checks, failure, views;
static X86_VGPU_MAPPING owner;
static KERNEL_BOOT_CONTEXT context;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGING_INVENTORY inventory;
static KERNEL_ACPI_DIRECTORY directory;
__declspec(align(8)) static unsigned char common[64];
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL native:%u %s\n",(unsigned int)__LINE__,#x);exit(1);} } while (0)

// ====== mocks ======

const X86_PAGING_GPU_AUTH *x86_paging_gpu_owner(void) { return failure==1?0:&owner.Authority; }
int x86_vgpu_authority(const X86_PAGING_GPU_AUTH *auth) { return failure!=2 && auth==&owner.Authority; }
int x86_vgpu_pci_match(const KERNEL_VGPU_BOOT *boot,BOOT_U32 bits) { (void)boot; (void)bits; return failure!=3; }
uint32_t kernel_vgpu_pci_probe(const unsigned char *config,uint32_t bytes,const uint64_t *sizes,
    uint32_t count,uint32_t bits,KERNEL_VGPU_PCI_LAYOUT *layout)
{
    (void)config; (void)bytes; (void)sizes; (void)count; (void)bits;
    memset(layout,0,sizeof(*layout)); layout->Regions[0].Length=failure==4?100000:56;
    return KERNEL_VGPU_PCI_READY;
}
int x86_page_window_read_acpi(X86_PAGE_WINDOW *loan,const KERNEL_BOOT_CONTEXT *boot,
    BOOT_U64 physical,BOOT_U64 bytes,unsigned char *output)
{
    (void)physical; CHECK(loan==&window && boot==&context && bytes==1);
    *output=7; return 1;
}
int x86_pm_clock_capture(const KERNEL_ACPI_DIRECTORY *tables,BOOT_U32 bits,KERNEL_ACPI_READ read,
    void *opaque,X86_PM_CLOCK *clock)
{
    unsigned char byte;
    CHECK(tables==&directory && bits==52 && read(opaque,0,1,&byte) && byte==7);
    *clock=(X86_PM_CLOCK){0,116,0x608,0xffffff,1};
    return failure!=5;
}
BOOT_U32 x86_pm_read(unsigned short port) { CHECK(port==0x608); return 0x12345678; }
volatile unsigned char *x86_vgpu_register_view(BOOT_U64 address,uint32_t width)
{
    CHECK(address>=X86_GPU_VIRTUAL && address-X86_GPU_VIRTUAL<=64-width);
    ++views;
    return common+(address-X86_GPU_VIRTUAL);
}
static int capture_private(const KERNEL_BOOT_CONTEXT *boot,const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *snapshot)
{
    (void)lapic; CHECK(boot==&context && allocator==&pool && snapshot==&inventory);
    return failure!=6;
}

// ====== main ======

int main(void)
{
    for (uint32_t cycle=0; cycle<100; ++cycle) {
        X86_VGPU_TRANSPORT transport={0}, copied;
        KERNEL_VGPU_IO io={0};
        uint32_t value;
        failure=0; views=0; memset(&owner,0,sizeof(owner)); memset(common,0,sizeof(common));
        owner.Self=(BOOT_U64)&owner; owner.Published=1; owner.Registered=1;
        owner.Context=&context; owner.Window=&window; window.Pool=&pool;
        owner.Inventory=&inventory; owner.Capture=capture_private;
        owner.Authority.PhysicalBits=52; owner.Authority.Plan.Count=1;
        CHECK(x86_vgpu_transport_open(&owner,&directory,&transport,&io));
        CHECK(io.Mask==0xffffff && io.Deadline==357955 && io.Samples==1000000);
        value=3; CHECK(io.Register(io.Context,20,1,1,&value) && common[20]==3);
        value=0; CHECK(io.Register(io.Context,20,1,0,&value) && value==3);
        value=0x12345678; CHECK(io.Register(io.Context,12,4,1,&value));
        value=0; CHECK(io.Register(io.Context,12,4,0,&value) && value==0x12345678);
        CHECK(io.Clock(io.Context,&value) && value==0x345678);
        uint32_t before=views;
        for (uint32_t offset=0; offset<64; ++offset)
            for (uint32_t width=0; width<9; ++width) {
                if (kernel_vgpu_register_valid(offset,width,0)) continue;
                CHECK(!io.Register(io.Context,offset,width,0,&value));
            }
        CHECK(views==before);
        value=4; CHECK(!io.Register(io.Context,20,1,1,&value));
        CHECK(!io.Register(io.Context,4,4,1,&value));
        CHECK(!io.Register(io.Context,20,1,0,(uint32_t *)&transport));
        CHECK(!io.Register(io.Context,20,1,0,(uint32_t *)&owner));
        CHECK(!io.Register(io.Context,20,1,0,0));
        copied=transport; CHECK(!x86_vgpu_transport_end(&copied));
        CHECK(x86_vgpu_transport_end(&transport));
        for (failure=1; failure<=6; ++failure) {
            X86_VGPU_TRANSPORT rejected={0}; KERNEL_VGPU_IO untouched={0}, original=untouched;
            CHECK(!x86_vgpu_transport_open(&owner,&directory,&rejected,&untouched));
            CHECK(!memcmp(&untouched,&original,sizeof(original)));
        }
        failure=0;
        ++transport.Base; CHECK(!io.Register(io.Context,20,1,0,&value)); --transport.Base;
        transport.Bytes=20; CHECK(!io.Register(io.Context,20,1,0,&value));
        owner.Self=0; CHECK(!x86_vgpu_transport_end(&transport) && !io.Clock(io.Context,&value));
    }
    printf("PASS: VirtIO native transport %u checks, widths, ownership, bounds and failure isolation\n",checks);
    return 0;
}
