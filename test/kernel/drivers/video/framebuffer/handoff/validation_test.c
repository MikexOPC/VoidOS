#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/video/framebuffer/handoff.h"
#include "arch/x86/boot/uefi/graphics/handoff.h"

static uint32_t checks;
#define CHECK(value) do { ++checks; assert(value); } while (0)
__declspec(align(8)) static unsigned char records[96];

// ====== put_field ======

static void put_field(unsigned int offset, uint64_t value, unsigned int bytes)
{
    unsigned int index;
    for (index = 0; index < bytes; ++index) {
        records[offset + index] = (unsigned char)value;
        value >>= 8;
    }
}

// ====== initialize_map ======

static void initialize_map(uint32_t type, uint64_t physical, uint64_t pages)
{
    memset(records,0,sizeof(records));
    put_field(0,type,4);
    put_field(8,physical,8);
    put_field(24,pages,8);
    put_field(48,7,4);
    put_field(56,0x100000,8);
    put_field(72,32,8);
}

// ====== numeric_handoff_validation ======

static void numeric_handoff_validation(void)
{
    OS_FRAMEBUFFER fb = {0};
    uint32_t index;
    CHECK(fb_handoff_valid(&fb,32));
    CHECK(!fb_handoff_valid(0,32));
    CHECK(!fb_handoff_valid(&fb,31));
    CHECK(!fb_handoff_valid(&fb,53));
    for (index = 0; index < sizeof(fb); ++index) {
        ((unsigned char *)&fb)[index] = 1;
        CHECK(!fb_handoff_valid(&fb,52));
        ((unsigned char *)&fb)[index] = 0;
    }
    fb = (OS_FRAMEBUFFER){0x80000000,4096,40,3,2,5,1};
    CHECK(fb_handoff_valid(&fb,32));
    fb.Used = 39;
    CHECK(!fb_handoff_valid(&fb,32));
    fb.Used = 40; fb.Capacity = 39;
    CHECK(!fb_handoff_valid(&fb,32));
}

// ====== resources_and_rollback ======

static void resources_and_rollback(void)
{
    BOOT_START_INFO info = {0};
    BOOT_MEMORY_MAP map = {records,sizeof(records),sizeof(records),1,48,1,1};
    OS_FRAMEBUFFER fb = {0x80000000,4096,40,3,2,5,1};
    uint32_t type, repeat;
    unsigned char saved[sizeof(records)];
    info.Signature = BOOT_START_SIGNATURE;
    info.Version = BOOT_START_VERSION;
    info.Size = sizeof(info);
    info.RequiredFeatures = BOOT_START_FEATURES;
    for (type = 0; type < 16; ++type) {
        initialize_map(type,fb.Base,1);
        CHECK(fb_handoff_memory_valid(&fb,records,96,48,32) == (type == 0 || type == 11));
    }
    initialize_map(11,fb.Base,1);
    CHECK(boot_attach_framebuffer_snapshot(&info,&map,&fb,32) == EFI_SUCCESS);
    CHECK(memcmp(&info.Framebuffer,&fb,sizeof(fb)) == 0);
    initialize_map(7,fb.Base,1);
    CHECK(boot_attach_framebuffer_snapshot(&info,&map,&fb,32) == EFI_LOAD_ERROR);
    CHECK(memcmp(&info.Framebuffer,&fb,sizeof(fb)) == 0);
    CHECK(boot_attach_framebuffer_snapshot(&info,&map,&info.Framebuffer,32) == EFI_INVALID_PARAMETER);
    info.Version = 3;
    CHECK(boot_attach_framebuffer_snapshot(&info,&map,&fb,32) == EFI_INVALID_PARAMETER);
    info.Version = BOOT_START_VERSION;
    info.Framebuffer = (OS_FRAMEBUFFER){0};
    CHECK(boot_validate_framebuffer_snapshot(&info,&map,32) == EFI_SUCCESS);
    initialize_map(7,fb.Base + 4096,1);
    CHECK(fb_handoff_memory_valid(&fb,records,96,48,32));
    fb.Base += 4;
    CHECK(!fb_handoff_memory_valid(&fb,records,96,48,32));
    fb.Base -= 4;
    CHECK(!fb_handoff_memory_valid(&fb,records,95,48,32));
    CHECK(!fb_handoff_memory_valid(&fb,records,96,39,32));
    CHECK(!fb_handoff_memory_valid(&fb,records,0,48,32));
    CHECK(!fb_handoff_memory_valid(&fb,0,96,48,32));
    CHECK(!fb_handoff_memory_valid(&fb,records,4097ULL * 48,48,32));
    initialize_map(11,fb.Base,0);
    CHECK(!fb_handoff_memory_valid(&fb,records,96,48,32));
    initialize_map(11,UINT64_MAX - 4095,1);
    CHECK(!fb_handoff_memory_valid(&fb,records,96,48,52));
    initialize_map(11,fb.Base,1);
    memcpy(saved,records,sizeof(saved));
    for (repeat = 0; repeat < 10000; ++repeat) {
        CHECK(boot_attach_framebuffer_snapshot(&info,&map,&fb,32) == EFI_SUCCESS);
        CHECK(boot_validate_framebuffer_snapshot(&info,&map,32) == EFI_SUCCESS);
        CHECK(memcmp(records,saved,sizeof(saved)) == 0);
    }
}

// ====== handoff_test_main ======

int main(void)
{
    numeric_handoff_validation();
    resources_and_rollback();
    printf("PASS: framebuffer handoff %u checks,10000 publication cycles,no allocations\n",checks);
    return 0;
}
