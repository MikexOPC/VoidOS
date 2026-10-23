#include "protocol.h"
#include "lib/memory/address_ranges.h"

// ====== valid_private ======

static int valid_private(const KERNEL_VGPU_IO *io,const KERNEL_VGPU_NEGOTIATION *result)
{
    return io && result && io->Context && io->Register && io->Clock &&
        (io->Mask==0xffffff || io->Mask==UINT32_MAX) && io->Deadline &&
        io->Deadline<io->Mask/2 && io->Samples && io->Samples<=1000000 && io->Platform<=1 &&
        !os_address_ranges_overlap((uintptr_t)io,sizeof(*io),(uintptr_t)result,sizeof(*result));
}

// ====== access_private ======

static int access_private(const KERNEL_VGPU_IO *io,uint32_t offset,uint32_t width,
    uint32_t write,uint32_t *value,KERNEL_VGPU_NEGOTIATION *result)
{
    if (!kernel_vgpu_register_valid(offset,width,write) ||
        !io->Register(io->Context,offset,width,write,value)) {
        result->Fault=KERNEL_VGPU_INIT_IO;
        return 0;
    }
    return 1;
}

// ====== kernel_vgpu_reset ======

// VirtIO1.2 2.4/4.1.4.3: status byte at common+0x14; zero initiates reset.
int kernel_vgpu_reset(const KERNEL_VGPU_IO *io,KERNEL_VGPU_NEGOTIATION *result)
{
    uint32_t start, now, value=0, progressed=0;
    if (!valid_private(io,result)) return 0;
    result->Reset=0; result->Polls=0;
    if (!io->Clock(io->Context,&start) || start>io->Mask) {
        result->Fault=KERNEL_VGPU_INIT_CLOCK; return 0;
    }
    if (!access_private(io,20,1,1,&value,result)) return 0;
    for (uint32_t count=0; count<io->Samples; ++count) {
        if (!access_private(io,20,1,0,&value,result)) return 0;
        ++result->Polls; result->Status=value;
        if (!io->Clock(io->Context,&now) || now>io->Mask) {
            result->Fault=KERNEL_VGPU_INIT_CLOCK; return 0;
        }
        uint32_t delta=(now-start)&io->Mask;
        if (delta>=io->Deadline) { result->Fault=KERNEL_VGPU_INIT_TIMEOUT; return 0; }
        if (delta) progressed=1;
        if (!value && progressed) { result->Reset=1; return 1; }
    }
    result->Fault=progressed ? KERNEL_VGPU_INIT_TIMEOUT : KERNEL_VGPU_INIT_CLOCK;
    return 0;
}

// ====== status_private ======

static int status_private(const KERNEL_VGPU_IO *io,uint32_t expected,KERNEL_VGPU_NEGOTIATION *result)
{
    uint32_t value=expected;
    result->Reset=0;
    if (!access_private(io,20,1,1,&value,result) || !access_private(io,20,1,0,&value,result)) return 0;
    result->Status=value;
    if (value != expected) { result->Fault=KERNEL_VGPU_INIT_STATUS; return 0; }
    result->Reset=0;
    return 1;
}

// ====== kernel_vgpu_negotiate ======

// VirtIO1.2 3.1.1: ACK1,DRIVER2,FEATURES_OK8; no DRIVER_OK before queues.
int kernel_vgpu_negotiate(const KERNEL_VGPU_IO *io,KERNEL_VGPU_NEGOTIATION *result)
{
    uint32_t value;
    if (!valid_private(io,result)) return 0;
    *result=(KERNEL_VGPU_NEGOTIATION){0};
    if (!kernel_vgpu_reset(io,result) || !status_private(io,1,result) || !status_private(io,3,result)) return 0;
    for (uint32_t select=0; select<2; ++select) {
        value=select;
        if (!access_private(io,0,4,1,&value,result) || !access_private(io,4,4,0,&value,result)) return 0;
        result->Offered |= (uint64_t)value<<(select*32);
    }
    result->Accepted=kernel_vgpu_features(result->Offered,io->Platform);
    if (!result->Accepted) { result->Fault=KERNEL_VGPU_INIT_FEATURE; return 0; }
    for (uint32_t select=0; select<2; ++select) {
        value=select;
        if (!access_private(io,8,4,1,&value,result)) return 0;
        value=(uint32_t)(result->Accepted>>(select*32));
        if (!access_private(io,12,4,1,&value,result)) return 0;
    }
    return status_private(io,11,result);
}
