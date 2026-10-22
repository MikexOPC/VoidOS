#ifndef X86_VGPU_RESOURCE_PROBE_H
#define X86_VGPU_RESOURCE_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
int x86_vgpu_resource_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io);
#endif
