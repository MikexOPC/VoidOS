#ifndef TEST_BOOT_VGPU_FIXTURE_H
#define TEST_BOOT_VGPU_FIXTURE_H
#include "kernel/drivers/video/virtio/discovery/handoff.h"

// ====== test_vgpu_put ======

static inline void test_vgpu_put(unsigned char *config, uint32_t offset, uint32_t value)
{
    for (uint32_t byte = 0; byte < 4; ++byte) config[offset+byte] = (unsigned char)(value >> (8*byte));
}

// ====== test_vgpu_fixture ======

static inline void test_vgpu_fixture(KERNEL_VGPU_BOOT *info)
{
    const uint32_t positions[4] = {0x40,0x50,0x64,0x74};
    const uint32_t lengths[4] = {56,4096,1,16};
    for (uint32_t byte = 0; byte < sizeof(*info); ++byte) ((volatile unsigned char *)info)[byte] = 0;
    info->Present=1; info->Device=1; info->Sizes[4]=16384;
    test_vgpu_put(info->Config,0,0x10501af4);
    info->Config[6]=16; info->Config[11]=3; info->Config[0x34]=0x40;
    test_vgpu_put(info->Config,0x20,4); test_vgpu_put(info->Config,0x24,1);
    for (uint32_t index=0; index<4; ++index) {
        uint32_t p=positions[index];
        info->Config[p]=9; info->Config[p+1]=(unsigned char)(index==3?0:positions[index+1]);
        info->Config[p+2]=(unsigned char)(index==1?20:16);
        info->Config[p+3]=(unsigned char)(index+1); info->Config[p+4]=4;
        test_vgpu_put(info->Config,p+8,index*4096); test_vgpu_put(info->Config,p+12,lengths[index]);
    }
    test_vgpu_put(info->Config,0x60,4);
}
#endif
