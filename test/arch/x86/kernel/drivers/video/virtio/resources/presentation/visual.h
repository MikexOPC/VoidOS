#ifndef X86_VGPU_VISUAL_PROBE_H
#define X86_VGPU_VISUAL_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include "kernel/drivers/video/virtio/resources/scanout.h"
// device: the backing address published to the GPU (frame, or IOVA under VT-d).
int x86_vgpu_visual_probe(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,X86_SURFACE_MAPPING *surface,uint64_t device);
#endif
