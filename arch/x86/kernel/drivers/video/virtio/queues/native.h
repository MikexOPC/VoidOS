#ifndef X86_VGPU_QUEUE_NATIVE_H
#define X86_VGPU_QUEUE_NATIVE_H
#include "../initialization/transport.h"
#include "kernel/drivers/video/virtio/queues/queue.h"
typedef struct {
    BOOT_U64  Self;
    X86_VGPU_TRANSPORT  *Transport;
    KERNEL_VGPU_PCI_LAYOUT  Layout;
} X86_VGPU_QUEUE_TRANSPORT;
int x86_vgpu_queue_open(X86_VGPU_TRANSPORT *transport,X86_VGPU_QUEUE_TRANSPORT *native,VGPU_QUEUE_IO_CONTEXT *io);
int x86_vgpu_queue_register(void *opaque,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value);
int x86_vgpu_queue_clock(void *opaque,uint32_t *value);
int x86_vgpu_queue_barrier(void *opaque);
int x86_vgpu_queue_pci(const KERNEL_VGPU_BOOT *gpu);
volatile unsigned char *x86_vgpu_queue_view(BOOT_U64 address,uint32_t width);
#endif
