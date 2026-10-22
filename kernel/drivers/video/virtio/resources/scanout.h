#ifndef KERNEL_VGPU_SCANOUT_H
#define KERNEL_VGPU_SCANOUT_H
#include "resource.h"
int kernel_vgpu_scanout_bind(VGPU_QUEUE *queue,VGPU_RESOURCE *resource);
int kernel_vgpu_scanout_update(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint32_t x,uint32_t y,uint32_t width,uint32_t height);
int kernel_vgpu_scanout_unbind(VGPU_QUEUE *queue,VGPU_RESOURCE *resource);
#endif
