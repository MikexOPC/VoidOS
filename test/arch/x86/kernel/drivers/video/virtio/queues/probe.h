#ifndef X86_VGPU_QUEUE_PROBE_H
#define X86_VGPU_QUEUE_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include "arch/x86/mm/dma/arena.h"
int x86_vgpu_queue_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io);
int x86_vgpu_queue_dimensions(uint32_t *width,uint32_t *height);
// Four queue pages in the persistent DMA arena (V2a): views, frames and identity device addresses.
int x86_vgpu_queue_buffers(X86_VGPU_MAPPING *owner,X86_DMA_ARENA *arena,VGPU_BUFFERS *buffers);
#endif
