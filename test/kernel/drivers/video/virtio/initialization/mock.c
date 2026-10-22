#include "fixture.h"
uint32_t checks;

// ====== test_fixture ======

void test_fixture(TEST_DEVICE *device,KERNEL_VGPU_IO *io)
{
    memset(device,0,sizeof(*device));
    device->Offered=UINT64_MAX; device->Step=1; device->Mask=0xffffff;
    *io=(KERNEL_VGPU_IO){device,test_register,test_clock,device->Mask,100,32};
}

// ====== test_clock ======

int test_clock(void *context,uint32_t *value)
{
    TEST_DEVICE *device=context;
    ++device->ClockCalls;
    if (device->ClockFail && device->ClockCalls==device->ClockFail) return 0;
    *value=device->Tick;
    if (!device->Stuck) device->Tick=(device->Tick+device->Step)&device->Mask;
    return 1;
}

// ====== test_register ======

int test_register(void *context,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    TEST_DEVICE *device=context;
    CHECK(kernel_vgpu_register_valid(offset,width,write));
    CHECK(device->Count<128);
    TEST_ACCESS *entry=&device->Trace[device->Count++];
    *entry=(TEST_ACCESS){offset,width,write,*value};
    if (device->FailAt && device->Count==device->FailAt) return 0;
    if (offset==20) {
        if (write) {
            CHECK(!(*value&4));
            if (*value) CHECK((*value&device->Status)==device->Status);
            device->Status=(*value==11 && device->Reject)?3:*value;
            if (!*value) device->Reads=0;
        } else {
            *value=device->Status;
            if (!device->Status && device->Reads++<device->Delay) *value=3;
            if (device->NeedReset && device->Status) *value |= 64;
        }
    } else if (write && offset==0) { CHECK(*value<2); device->DeviceSelect=*value; }
    else if (write && offset==8) { CHECK(*value<2); device->DriverSelect=*value; }
    else if (!write && offset==4) *value=(uint32_t)(device->Offered>>(device->DeviceSelect*32));
    else if (write && offset==12) {
        CHECK(device->Status==3);
        uint64_t mask=(uint64_t)UINT32_MAX<<(device->DriverSelect*32);
        device->Accepted=(device->Accepted&~mask)|((uint64_t)*value<<(device->DriverSelect*32));
    } else CHECK(0);
    entry->Value=*value;
    return 1;
}
