#include "native.h"
#include "lib/memory/address_ranges.h"
#include <intrin.h>

// ====== live_private ======

static int live_private(const X86_VGPU_QUEUE_TRANSPORT *native)
{
    const X86_VGPU_TRANSPORT *transport;
    return native && native->Self==(BOOT_U64)native && (transport=native->Transport) &&
        transport->Self==(BOOT_U64)transport && transport->Owner &&
        transport->Owner->Self==(BOOT_U64)transport->Owner &&
        transport->Base==X86_GPU_VIRTUAL+transport->Owner->Authority.Plan.Offset[0] &&
        x86_paging_gpu_owner()==&transport->Owner->Authority && x86_vgpu_authority(&transport->Owner->Authority);
}

// ====== x86_vgpu_queue_open ======

int x86_vgpu_queue_open(X86_VGPU_TRANSPORT *transport,X86_VGPU_QUEUE_TRANSPORT *native,VGPU_QUEUE_IO_CONTEXT *io)
{
    int cpu[4];
    __cpuid(cpu,1);
    if (!(cpu[3]&(1U<<26))) return 0;
    if (!transport || !native || !io || !x86_vgpu_transport_end(transport) ||
        !x86_vgpu_queue_pci(&transport->Owner->Context->Info.Gpu) ||
        os_address_ranges_overlap((BOOT_U64)native,sizeof(*native),(BOOT_U64)transport,sizeof(*transport)) ||
        os_address_ranges_overlap((BOOT_U64)native,sizeof(*native),(BOOT_U64)transport->Owner,sizeof(*transport->Owner)) ||
        os_address_ranges_overlap((BOOT_U64)io,sizeof(*io),(BOOT_U64)transport->Owner,sizeof(*transport->Owner)) ||
        os_address_ranges_overlap((BOOT_U64)io,sizeof(*io),(BOOT_U64)native,sizeof(*native)) ||
        os_address_ranges_overlap((BOOT_U64)io,sizeof(*io),(BOOT_U64)transport,sizeof(*transport))) return 0;
    KERNEL_VGPU_PCI_LAYOUT layout;
    if (kernel_vgpu_pci_probe(transport->Owner->Context->Info.Gpu.Config,256,
        transport->Owner->Context->Info.Gpu.Sizes,6,transport->Owner->Authority.PhysicalBits,&layout)!=KERNEL_VGPU_PCI_READY) return 0;
    *native=(X86_VGPU_QUEUE_TRANSPORT){(BOOT_U64)native,transport,layout};
    *io=(VGPU_QUEUE_IO_CONTEXT){native,x86_vgpu_queue_register,x86_vgpu_queue_clock,x86_vgpu_queue_barrier,
        transport->Clock.Mask,357955,1000000};
    return 1;
}

// ====== x86_vgpu_queue_barrier ======

// Intel SDM Vol2B MFENCE instruction: order prior loads/stores before subsequent accesses.
// VirtIO1.2 2.7.13.3.1/2.7.14: publish/consume coherent WB rings before UC notify.
// SSE2 is part of the supported x86-64 CPU profile. No SIMD data access to MMIO.
int x86_vgpu_queue_barrier(void *opaque)
{
    if (!live_private(opaque)) return 0;
    _ReadWriteBarrier(); _mm_mfence(); _ReadWriteBarrier();
    return 1;
}

// ====== x86_vgpu_queue_clock ======

int x86_vgpu_queue_clock(void *opaque,uint32_t *value)
{
    const X86_VGPU_QUEUE_TRANSPORT *native=opaque;
    return value && live_private(native) && x86_vgpu_transport_clock(native->Transport,value);
}

// ====== x86_vgpu_queue_register ======

// VirtIO1.2 4.1.4.3/4.1.4.4/4.1.4.5/5.7.4: common fields, u16 notify, read-clear ISR byte,
// and device-specific config (region 3: offset 0 events_read R/O, offset 4 events_clear W/O).
// Notify address = capability base + queue_notify_off * multiplier; never an unbounded pointer.
int x86_vgpu_queue_register(void *opaque,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    const X86_VGPU_QUEUE_TRANSPORT *native=opaque;
    if (!value || !live_private(native) || region>3 || write>1 ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)native,sizeof(*native)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)native->Transport,sizeof(*native->Transport)) ||
        os_address_ranges_overlap((BOOT_U64)value,4,(BOOT_U64)native->Transport->Owner,sizeof(*native->Transport->Owner))) return 0;
    const KERNEL_VGPU_PCI_REGION *cap=&native->Layout.Regions[region];
    BOOT_U64 at=offset;
    if (!region) {
        if (!kernel_vgpu_queue_register(offset,width,write)) return 0;
        if (write && ((offset==20 && *value!=15) || (offset==0x1c && *value!=1) ||
            (offset==0x18 && *value!=8) || (offset==0x16 && *value>1) ||
            ((offset==0x10 || offset==0x1a) && *value!=0xffff))) return 0;
    } else if (region==1) {
        if (width!=2 || !write || *value>1 || offset>65535) return 0;
        at=(BOOT_U64)offset*cap->Multiplier;
    } else if (region==2) {
        if (width!=1 || write || offset) return 0;
    } else {
        if (width!=4 || (offset==0 && write) || (offset==4 && !write) || (offset!=0 && offset!=4)) return 0;
    }
    const X86_VGPU_MAPPING *owner=native->Transport->Owner;
    if (!width || (width!=1 && width!=2 && width!=4) || at>cap->Length || width>cap->Length-at ||
        (width==2 && write && *value>65535)) return 0;
    BOOT_U64 relative=owner->Authority.Plan.Offset[region]+at;
    if (relative>owner->Authority.Plan.Count*4096ULL || width>owner->Authority.Plan.Count*4096ULL-relative) return 0;
    BOOT_U64 address=X86_GPU_VIRTUAL+relative;
    if (address&(width-1)) return 0;
    volatile unsigned char *view=x86_vgpu_queue_view(address,width);
    if (!view) return 0;
    _ReadWriteBarrier();
    if (width==1) {
        volatile unsigned char *reg=view;
        if (write) *reg=(unsigned char)*value; else *value=*reg;
    } else if (width==2) {
        volatile uint16_t *reg=(volatile uint16_t *)view;
        if (write) *reg=(uint16_t)*value; else *value=*reg;
    } else {
        volatile uint32_t *reg=(volatile uint32_t *)view;
        if (write) *reg=*value; else *value=*reg;
    }
    _ReadWriteBarrier();
    return 1;
}
