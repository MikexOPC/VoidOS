#ifndef KERNEL_VGPU_RESOURCE_H
#define KERNEL_VGPU_RESOURCE_H
#include "kernel/drivers/video/virtio/queues/queue.h"
uint64_t kernel_vgpu_surface_bytes(uint32_t width,uint32_t height);
uint64_t kernel_vgpu_surface_entry(uint32_t table,uint32_t slot,uint64_t base,uint64_t pages,uint64_t child);
uint32_t kernel_vgpu_resource_transition(uint32_t state,uint32_t command);
uint64_t kernel_vgpu_surface_offset(uint32_t width,uint32_t height,uint32_t x,uint32_t y,uint32_t w,uint32_t h);
typedef struct { uint64_t Self, Base, Bytes, Queue; uint32_t Width, Height, Id, State, Bound, Scanout; } VGPU_RESOURCE;
int kernel_vgpu_resource_open(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint64_t physical,uint32_t width,uint32_t height);
int kernel_vgpu_resource_close(VGPU_QUEUE *queue,VGPU_RESOURCE *resource);
int kernel_vgpu_resource_run(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint64_t physical,uint32_t width,uint32_t height);
#endif
