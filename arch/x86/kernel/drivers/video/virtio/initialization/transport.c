#include "transport.h"
#include "lib/memory/address_ranges.h"
#include <intrin.h>

// ====== read_private ======

static int read_private(void *opaque,BOOT_U64 physical,BOOT_U32 bytes,unsigned char *output)
{
    const X86_VGPU_MAPPING *owner=opaque;
    return x86_page_window_read_acpi(owner->Window,owner->Context,physical,bytes,output);
}

// ====== x86_vgpu_transport_end ======

int x86_vgpu_transport_end(const X86_VGPU_TRANSPORT *transport)
{
    const X86_VGPU_MAPPING *owner;
    return transport && transport->Self==(BOOT_U64)transport && (owner=transport->Owner) &&
        owner->Self==(BOOT_U64)owner && owner->Capture && owner->Window && owner->Published==1 && owner->Registered==1 &&
        x86_paging_gpu_owner()==&owner->Authority && x86_vgpu_authority(&owner->Authority) &&
        owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory);
}

// ====== x86_vgpu_transport_open ======

int x86_vgpu_transport_open(X86_VGPU_MAPPING *owner,const KERNEL_ACPI_DIRECTORY *directory,
    X86_VGPU_TRANSPORT *transport,KERNEL_VGPU_IO *io)
{
    KERNEL_VGPU_PCI_LAYOUT layout;
    X86_VGPU_TRANSPORT draft={0};
    if (!owner || !transport || !io || !directory || owner->Self!=(BOOT_U64)owner ||
        !owner->Context || !owner->Window || !owner->Capture || !owner->Inventory ||
        os_address_ranges_overlap((BOOT_U64)transport,sizeof(*transport),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)io,sizeof(*io),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)io,sizeof(*io),(BOOT_U64)transport,sizeof(*transport)) ||
        !x86_vgpu_authority(&owner->Authority) ||
        !x86_vgpu_pci_match(&owner->Context->Info.Gpu,owner->Authority.PhysicalBits) ||
        kernel_vgpu_pci_probe(owner->Context->Info.Gpu.Config,256,owner->Context->Info.Gpu.Sizes,6,
            owner->Authority.PhysicalBits,&layout)!=KERNEL_VGPU_PCI_READY ||
        !x86_pm_clock_capture(directory,owner->Authority.PhysicalBits,read_private,owner,&draft.Clock)) return 0;
    draft.Owner=owner; draft.Self=(BOOT_U64)transport;
    draft.Base=X86_GPU_VIRTUAL+owner->Authority.Plan.Offset[0]; draft.Bytes=layout.Regions[0].Length;
    if (draft.Base<X86_GPU_VIRTUAL || draft.Bytes<56 ||
        draft.Base-X86_GPU_VIRTUAL>owner->Authority.Plan.Count*4096ULL ||
        draft.Bytes>owner->Authority.Plan.Count*4096ULL-(draft.Base-X86_GPU_VIRTUAL) ||
        (draft.Base&3) || draft.Clock.Ready!=1 || !draft.Clock.Port || draft.Clock.Port>65535 ||
        (draft.Clock.Mask!=0xffffff && draft.Clock.Mask!=UINT32_MAX)) return 0;
    *transport=draft;
    if (!x86_vgpu_transport_end(transport)) {
        *transport=(X86_VGPU_TRANSPORT){0}; return 0;
    }
    *io=(KERNEL_VGPU_IO){transport,x86_vgpu_transport_register,x86_vgpu_transport_clock,
        draft.Clock.Mask,357955,1000000};
    return 1;
}

// ====== live_private ======

static int live_private(const X86_VGPU_TRANSPORT *transport)
{
    return transport && transport->Self==(BOOT_U64)transport && transport->Owner &&
        transport->Owner->Self==(BOOT_U64)transport->Owner &&
        transport->Base==X86_GPU_VIRTUAL+transport->Owner->Authority.Plan.Offset[0] &&
        x86_paging_gpu_owner()==&transport->Owner->Authority &&
        x86_vgpu_authority(&transport->Owner->Authority);
}

// ====== x86_vgpu_transport_register ======

// VirtIO1.2 4.1.4.3: common select0/8:u32,feature4/12:u32,status20:u8.
// Intel SDM3A14.3: strong UC, scalar GPR access; barriers constrain compiler.
int x86_vgpu_transport_register(void *opaque,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    const X86_VGPU_TRANSPORT *transport=opaque;
    if (!value || !live_private(transport) || !kernel_vgpu_register_valid(offset,width,write) ||
        offset>transport->Bytes || width>transport->Bytes-offset ||
        os_address_ranges_overlap((BOOT_U64)value,sizeof(*value),(BOOT_U64)transport,sizeof(*transport)) ||
        os_address_ranges_overlap((BOOT_U64)value,sizeof(*value),(BOOT_U64)transport->Owner,sizeof(*transport->Owner)) ||
        (width==1 && write && (*value & ~0x8bU))) return 0;
    BOOT_U64 address=transport->Base+offset;
    volatile unsigned char *view=x86_vgpu_register_view(address,width);
    if (!view) return 0;
    _ReadWriteBarrier();
    if (width==1) {
        volatile unsigned char *reg=view;
        if (write) *reg=(unsigned char)*value; else *value=*reg;
    } else {
        volatile uint32_t *reg=(volatile uint32_t *)view;
        if (write) *reg=*value; else *value=*reg;
    }
    _ReadWriteBarrier();
    return 1;
}

// ====== x86_vgpu_transport_clock ======

int x86_vgpu_transport_clock(void *opaque,uint32_t *value)
{
    const X86_VGPU_TRANSPORT *transport=opaque;
    if (!value || !live_private(transport)) return 0;
    *value=x86_pm_read((unsigned short)transport->Clock.Port)&transport->Clock.Mask;
    return 1;
}
