#ifndef KERNEL_VGPU_INITIALIZATION_H
#define KERNEL_VGPU_INITIALIZATION_H
#include <stdint.h>
#define KERNEL_VGPU_INIT_IO 1U
#define KERNEL_VGPU_INIT_CLOCK 2U
#define KERNEL_VGPU_INIT_TIMEOUT 3U
#define KERNEL_VGPU_INIT_FEATURE 4U
#define KERNEL_VGPU_INIT_STATUS 5U
typedef int (*KERNEL_VGPU_REGISTER)(void *,uint32_t,uint32_t,uint32_t,uint32_t *);
typedef int (*KERNEL_VGPU_CLOCK)(void *,uint32_t *);
typedef struct {
    void  *Context;
    KERNEL_VGPU_REGISTER  Register;
    KERNEL_VGPU_CLOCK  Clock;
    uint32_t  Mask, Deadline, Samples;
    // 1 only while an owned IOMMU domain translates this device (VIRTIO_F_ACCESS_PLATFORM).
    uint32_t  Platform;
} KERNEL_VGPU_IO;
typedef struct {
    uint64_t  Offered, Accepted;
    uint32_t  Fault, Status, Polls, Reset;
} KERNEL_VGPU_NEGOTIATION;
uint64_t kernel_vgpu_features(uint64_t offered,uint32_t platform);
uint32_t kernel_vgpu_register_valid(uint32_t offset,uint32_t width,uint32_t write);
int kernel_vgpu_reset(const KERNEL_VGPU_IO *io,KERNEL_VGPU_NEGOTIATION *result);
int kernel_vgpu_negotiate(const KERNEL_VGPU_IO *io,KERNEL_VGPU_NEGOTIATION *result);
#endif
