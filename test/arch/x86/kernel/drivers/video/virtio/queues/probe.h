#ifndef X86_VGPU_QUEUE_PROBE_H
#define X86_VGPU_QUEUE_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
int x86_vgpu_queue_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io);
int x86_vgpu_queue_dimensions(uint32_t *width,uint32_t *height);
#endif
