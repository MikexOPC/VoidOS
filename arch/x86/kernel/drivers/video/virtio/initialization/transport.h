#ifndef X86_VGPU_TRANSPORT_H
#define X86_VGPU_TRANSPORT_H
#include "../mapping.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/video/virtio/initialization/protocol.h"
typedef struct {
    BOOT_U64  Self, Base, Bytes;
    X86_VGPU_MAPPING  *Owner;
    X86_PM_CLOCK  Clock;
} X86_VGPU_TRANSPORT;
int x86_vgpu_transport_open(X86_VGPU_MAPPING *owner,const KERNEL_ACPI_DIRECTORY *directory,
    X86_VGPU_TRANSPORT *transport,KERNEL_VGPU_IO *io);
int x86_vgpu_transport_end(const X86_VGPU_TRANSPORT *transport);
int x86_vgpu_transport_register(void *opaque,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value);
int x86_vgpu_transport_clock(void *opaque,uint32_t *value);
volatile unsigned char *x86_vgpu_register_view(BOOT_U64 address,uint32_t width);
#endif
