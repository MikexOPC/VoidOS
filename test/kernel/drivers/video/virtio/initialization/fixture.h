#ifndef TEST_VGPU_PROTOCOL_FIXTURE_H
#define TEST_VGPU_PROTOCOL_FIXTURE_H
#include "kernel/drivers/video/virtio/initialization/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { uint32_t Offset, Width, Write, Value; } TEST_ACCESS;
typedef struct {
    TEST_ACCESS  Trace[128];
    uint64_t  Offered, Accepted;
    uint32_t  Count, Status, DeviceSelect, DriverSelect, Tick, Step;
    uint32_t  Mask, Reject, Delay, Reads, Stuck, NeedReset, FailAt, ClockFail, ClockCalls;
} TEST_DEVICE;
extern uint32_t checks;
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL %s:%u: %s\n",__FILE__,(unsigned int)__LINE__,#x);exit(1);} } while (0)
int test_register(void *context,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value);
int test_clock(void *context,uint32_t *value);
void test_fixture(TEST_DEVICE *device,KERNEL_VGPU_IO *io);
#endif
