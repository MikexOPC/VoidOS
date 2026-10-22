#include "kernel/drivers/video/virtio/discovery/pci.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static _Alignas(8) unsigned char config[256];
static uint64_t sizes[6];
static KERNEL_VGPU_PCI_LAYOUT output;
static uint32_t checks;
void run_config_read_tests(void);
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"Check failed at line %u: %s\n",(unsigned int)__LINE__,#test); \
    exit(1); } } while (0)

// ====== put_private ======

static void put_private(uint32_t position, uint32_t value, uint32_t count)
{
    for (uint32_t index = 0; index < count; ++index) config[position+index] = (unsigned char)(value >> (index*8));
}

// ====== fixture_private ======

static void fixture_private(void)
{
    const uint32_t positions[4] = {0x40,0x50,0x64,0x74};
    const uint32_t lengths[4] = {56,4096,1,16};
    memset(config,0,sizeof(config)); memset(sizes,0,sizeof(sizes));
    put_private(0,0x10501af4,4);
    config[6] = 16; config[8] = 1; config[11] = 3; config[0x34] = 0x40;
    put_private(0x20,4,4); put_private(0x24,1,4); sizes[4] = 16384;
    for (uint32_t index = 0; index < 4; ++index) {
        uint32_t p = positions[index];
        config[p] = 9; config[p+1] = (unsigned char)(index == 3 ? 0 : positions[index+1]);
        config[p+2] = (unsigned char)(index == 1 ? 20 : 16);
        config[p+3] = (unsigned char)(index+1); config[p+4] = 4;
        put_private(p+8,index*4096,4); put_private(p+12,lengths[index],4);
    }
    put_private(0x60,4,4);
}

// ====== probe_private ======

static uint32_t probe_private(void)
{
    return kernel_vgpu_pci_probe(config,256,sizes,6,48,&output);
}

// ====== unchanged_private ======

static void unchanged_private(uint32_t expected)
{
    KERNEL_VGPU_PCI_LAYOUT before;
    memset(&output,0xa5,sizeof(output)); before = output;
    CHECK(probe_private() == expected);
    CHECK(!memcmp(&before,&output,sizeof(output)));
}

// ====== positive_and_chain_private ======

static void positive_and_chain_private(void)
{
    fixture_private(); CHECK(probe_private() == KERNEL_VGPU_PCI_READY);
    CHECK(output.Capabilities == 4 && output.Regions[0].Address == UINT64_C(0x100000000));
    CHECK(output.Regions[1].Multiplier == 4 && output.Regions[3].Length == 16);
    config[0x0e] = 0x80; config[0x0a] = 0x80;
    CHECK(probe_private() == KERNEL_VGPU_PCI_READY);
    fixture_private(); memcpy(config+0x90,config+0x40,16); config[0x91] = 0x40;
    put_private(0x98,1024,4); config[0x34] = 0x90;
    CHECK(probe_private() == KERNEL_VGPU_PCI_READY && output.Regions[0].Offset == 1024);
    fixture_private(); config[0x75] = 0x40;
    unchanged_private(KERNEL_VGPU_PCI_REJECTED);
    fixture_private(); config[0x41] = 0x44;
    unchanged_private(KERNEL_VGPU_PCI_REJECTED);
    fixture_private(); config[0x34] = 0xfc; config[0xfc] = 9; config[0xfe] = 16;
    unchanged_private(KERNEL_VGPU_PCI_REJECTED);
    fixture_private(); config[0x54] = 5;
    unchanged_private(KERNEL_VGPU_PCI_UNSUPPORTED);
    fixture_private(); config[0x4c] = 0xff; config[0x4d] = 0xff;
    unchanged_private(KERNEL_VGPU_PCI_REJECTED);
    fixture_private(); put_private(0x7c,4096,4);
    unchanged_private(KERNEL_VGPU_PCI_REJECTED);
    fixture_private(); sizes[4] = 0;
    unchanged_private(KERNEL_VGPU_PCI_UNSUPPORTED);
    fixture_private(); config[0] = 0xff;
    unchanged_private(KERNEL_VGPU_PCI_ABSENT);
    fixture_private(); config[0x0e] = 1;
    unchanged_private(KERNEL_VGPU_PCI_UNSUPPORTED);
}

// ====== input_bounds_private ======

static void input_bounds_private(void)
{
    fixture_private();
    for (uint32_t count = 0; count < 256; ++count)
        CHECK(kernel_vgpu_pci_probe(config,count,sizes,6,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,257,sizes,6,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(NULL,256,sizes,6,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,256,NULL,6,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,256,sizes,5,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,256,sizes,6,48,NULL) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,256,sizes,6,48,(KERNEL_VGPU_PCI_LAYOUT *)config) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(kernel_vgpu_pci_probe(config,256,(const uint64_t *)&output,6,48,&output) == KERNEL_VGPU_PCI_REJECTED);
    CHECK(probe_private() == KERNEL_VGPU_PCI_READY);
}

// ====== mutation_private ======

static void mutation_private(void)
{
    fixture_private();
    for (uint32_t position = 0; position < 256; ++position) {
        unsigned char saved = config[position];
        for (uint32_t value = 0; value < 256; ++value) {
            KERNEL_VGPU_PCI_LAYOUT before;
            uint32_t status;
            config[position] = (unsigned char)value;
            memset(&output,0xa5,sizeof(output)); before = output;
            status = probe_private();
            CHECK(status <= KERNEL_VGPU_PCI_UNSUPPORTED);
            if (status != KERNEL_VGPU_PCI_READY) CHECK(!memcmp(&before,&output,sizeof(output)));
            else for (uint32_t index = 0; index < 4; ++index) {
                CHECK(output.Regions[index].Bar == 4 && output.Regions[index].Address >= UINT64_C(0x100000000));
                CHECK(output.Regions[index].Length <= 16384);
            }
        }
        config[position] = saved;
    }
}

// ====== real_snapshot_private ======

static int real_snapshot_private(const char *config_path, const char *sizes_path)
{
    FILE *stream = NULL;
    CHECK(!fopen_s(&stream,config_path,"rb") && stream);
    CHECK(fread(config,1,sizeof(config),stream) == sizeof(config));
    CHECK(fgetc(stream) == EOF && !ferror(stream)); CHECK(!fclose(stream));
    CHECK(!fopen_s(&stream,sizes_path,"rb") && stream);
    CHECK(fread(sizes,1,sizeof(sizes),stream) == sizeof(sizes));
    CHECK(fgetc(stream) == EOF && !ferror(stream)); CHECK(!fclose(stream));
    CHECK(probe_private() == KERNEL_VGPU_PCI_READY);
    printf("READY: caps=%u common=0x%llx notify=0x%llx isr=0x%llx device=0x%llx\n",
        output.Capabilities,(unsigned long long)output.Regions[0].Address,
        (unsigned long long)output.Regions[1].Address,(unsigned long long)output.Regions[2].Address,
        (unsigned long long)output.Regions[3].Address);
    return 0;
}

// ====== main ======

int main(int argc, char **argv)
{
    if (argc == 3) return real_snapshot_private(argv[1],argv[2]);
    CHECK(argc == 1);
    positive_and_chain_private(); input_bounds_private(); mutation_private();
    run_config_read_tests();
    printf("PASS: VirtIO GPU PCI discovery %u checks, C/Rust ABI, no allocations or device access\n",checks);
    return 0;
}
