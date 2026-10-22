#include "kernel/drivers/video/virtio/discovery/configuration/read.h"
#include "test/arch/x86/boot/uefi/pci/fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static KERNEL_VGPU_BOOT fixture;
static unsigned char output[256], saved[256];
static unsigned int checks, reads, failure, proxy_reads;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL config %u: %s\n",(unsigned)__LINE__,#value); exit(1); } } while (0)

// ====== read_private ======

static int read_private(void *context, uint32_t offset, uint32_t count, unsigned char *destination)
{
    CHECK(context==&fixture && offset<=256 && count<=256-offset && count);
    ++reads;
    if (offset<0xa4 && offset+count>0xa0) { ++proxy_reads; return 0; }
    if (reads==failure) return 0;
    memcpy(destination,fixture.Config+offset,count);
    return 1;
}

// ====== setup_private ======

static void setup_private(void)
{
    test_vgpu_fixture(&fixture);
    fixture.Config[0x75]=0x90;
    fixture.Config[0x90]=9; fixture.Config[0x92]=20; fixture.Config[0x93]=5;
    memset(fixture.Config+0xa0,0xff,4);
    memset(output,0xa5,sizeof(output)); memcpy(saved,output,sizeof(saved));
    reads=failure=proxy_reads=0;
}

// ====== run_config_read_tests ======

void run_config_read_tests(void)
{
    unsigned int total;
    KERNEL_VGPU_PCI_LAYOUT layout;
    setup_private();
    CHECK(kernel_vgpu_config_read(read_private,&fixture,output));
    total=reads;
    CHECK(!proxy_reads && output[0x93]==5 && output[0xa0]==0 && output[0xa3]==0);
    CHECK(kernel_vgpu_pci_probe(output,256,fixture.Sizes,6,48,&layout)==KERNEL_VGPU_PCI_READY);
    for (unsigned int at=1; at<=total; ++at) {
        setup_private(); failure=at;
        CHECK(!kernel_vgpu_config_read(read_private,&fixture,output));
        CHECK(!proxy_reads && !memcmp(output,saved,256));
    }
    for (unsigned int at=0; at<4; ++at) {
        setup_private();
        if (!at) fixture.Config[0x91]=0xa0;
        if (at==1) fixture.Config[0x91]=0x40;
        if (at==2) fixture.Config[0x52]=16;
        if (at==3) fixture.Config[0x92]=255;
        CHECK(!kernel_vgpu_config_read(read_private,&fixture,output));
        CHECK(!proxy_reads && !memcmp(output,saved,256));
    }
    CHECK(!kernel_vgpu_config_read(NULL,&fixture,output));
    CHECK(!kernel_vgpu_config_read(read_private,NULL,output));
    CHECK(!kernel_vgpu_config_read(read_private,&fixture,NULL));
    printf("PASS: selective PCI metadata %u checks; PCI_CFG data window never read\n",checks);
}
