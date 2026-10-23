#include "fixture.h"

// ====== main ======

int main(void)
{
    TEST_DEVICE device;
    KERNEL_VGPU_IO io;
    KERNEL_VGPU_NEGOTIATION result;
    uint32_t accesses;
    for (uint32_t cycle=0; cycle<100; ++cycle) {
        test_fixture(&device,&io);
        CHECK(kernel_vgpu_negotiate(&io,&result));
        CHECK(result.Status==11 && !result.Reset && !result.Fault && result.Accepted==(1ULL<<32));
        CHECK(device.Accepted==result.Accepted && result.Offered==UINT64_MAX);
        accesses=device.Count;
        CHECK(kernel_vgpu_reset(&io,&result) && result.Reset && !result.Status);
        CHECK(device.Status==0);
        for (uint32_t failure=1; failure<=accesses; ++failure) {
            test_fixture(&device,&io); device.FailAt=failure;
            CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_IO);
            device.FailAt=0;
            CHECK(kernel_vgpu_reset(&io,&result) && result.Reset);
        }
        test_fixture(&device,&io); device.Offered=~(1ULL<<32);
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_FEATURE);
        CHECK(!device.Accepted && kernel_vgpu_reset(&io,&result));
        // ACCESS_PLATFORM: accepted and required only while the IOMMU translates the device.
        test_fixture(&device,&io); io.Platform=1;
        CHECK(kernel_vgpu_negotiate(&io,&result) && result.Accepted==(3ULL<<32) && device.Accepted==(3ULL<<32));
        CHECK(kernel_vgpu_reset(&io,&result));
        test_fixture(&device,&io); io.Platform=1; device.Offered=~(1ULL<<33);
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_FEATURE && !device.Accepted);
        CHECK(kernel_vgpu_reset(&io,&result));
        test_fixture(&device,&io); io.Platform=2; device.Count=0;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && !device.Count);
        test_fixture(&device,&io); device.Reject=1;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_STATUS);
        CHECK(!result.Reset && kernel_vgpu_reset(&io,&result));
        test_fixture(&device,&io); device.NeedReset=1;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_STATUS);
        device.NeedReset=0; CHECK(kernel_vgpu_reset(&io,&result));
        test_fixture(&device,&io); device.Delay=5;
        CHECK(kernel_vgpu_negotiate(&io,&result) && result.Polls==6);
        test_fixture(&device,&io); device.Stuck=1;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_CLOCK && result.Polls==32);
        test_fixture(&device,&io); device.Delay=100; device.Step=25;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_TIMEOUT && result.Polls==4);
        for (uint32_t width=24; width<=32; width+=8) {
            test_fixture(&device,&io); device.Mask=width==24?0xffffff:UINT32_MAX; io.Mask=device.Mask;
            device.Tick=device.Mask-2; device.Delay=4;
            CHECK(kernel_vgpu_negotiate(&io,&result));
        }
        for (uint32_t failure=1; failure<=2; ++failure) {
            test_fixture(&device,&io); device.ClockFail=failure;
            CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_CLOCK);
        }
        test_fixture(&device,&io); device.Tick=0x1000000;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && result.Fault==KERNEL_VGPU_INIT_CLOCK && !device.Count);
        test_fixture(&device,&io); io.Samples=0;
        memset(&result,0xa5,sizeof(result)); KERNEL_VGPU_NEGOTIATION before=result;
        CHECK(!kernel_vgpu_negotiate(&io,&result) && !memcmp(&before,&result,sizeof(result)) && !device.Count);
        CHECK(!kernel_vgpu_negotiate(0,&result) && !kernel_vgpu_negotiate(&io,0));
        CHECK(kernel_vgpu_features(1ULL<<32,0)==(1ULL<<32) && !kernel_vgpu_features(0,0));
        CHECK(kernel_vgpu_features(3ULL<<32,1)==(3ULL<<32) && !kernel_vgpu_features(1ULL<<32,1) && !kernel_vgpu_features(3ULL<<32,2));
    }
    printf("PASS: VirtIO initialization %u checks, 100 cycles, all IO failure positions, bounded reset and retry\n",checks);
    return 0;
}
