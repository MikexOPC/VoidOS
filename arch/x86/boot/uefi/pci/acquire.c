#include "acquire.h"
#include "kernel/drivers/video/virtio/discovery/configuration/read.h"
static EFI_GUID pci_guid = {0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
typedef struct { BOOT_PCI_IO *Device; EFI_STATUS Status; } PCI_READER;

// ====== read_config_private ======

static int read_config_private(void *opaque, UINT32 offset, UINT32 bytes, unsigned char *output)
{
    PCI_READER *reader=opaque;
    reader->Status=reader->Device->Read(reader->Device,0,offset,bytes,output);
    return reader->Status==EFI_SUCCESS;
}

// ====== snapshot_private ======

static EFI_STATUS snapshot_private(BOOT_PCI_IO *pci, unsigned char *output)
{
    PCI_READER reader={pci,EFI_SUCCESS};
    if (kernel_vgpu_config_read(read_config_private,&reader,output)) return EFI_SUCCESS;
    return reader.Status==EFI_SUCCESS?EFI_UNSUPPORTED:reader.Status;
}

// ====== boot_vgpu_release ======

EFI_STATUS boot_vgpu_release(BOOT_PCI_OWNER *owner)
{
    EFI_STATUS status;
    if (!owner || !owner->Services || !owner->Services->Prefix.FreePool) return EFI_INVALID_PARAMETER;
    if (owner->Resources) {
        status = owner->Services->Prefix.FreePool(owner->Resources);
        if (status != EFI_SUCCESS) return status;
        owner->Resources = 0;
    }
    if (owner->Handles) {
        status = owner->Services->Prefix.FreePool(owner->Handles);
        if (status != EFI_SUCCESS) return status;
        owner->Handles = 0;
    }
    owner->HandleCount = 0;
    return EFI_SUCCESS;
}

// ====== dword_private ======

static UINT32 dword_private(const unsigned char *config, UINT32 at)
{
    return (UINT32)config[at] | ((UINT32)config[at+1]<<8) |
        ((UINT32)config[at+2]<<16) | ((UINT32)config[at+3]<<24);
}

// ====== resources_private ======

static EFI_STATUS resources_private(BOOT_PCI_IO *pci, UINT32 bits,
    BOOT_PCI_OWNER *owner, KERNEL_VGPU_BOOT *draft)
{
    for (UINT32 bar = 0; bar < 6; ++bar) {
        UINT32 low = dword_private(draft->Config,16+4*bar), high = 0;
        void *resources = 0;
        EFI_STATUS status;
        if (!low || (low & 1)) continue;
        if ((low & 7) == 4) {
            if (bar == 5) return EFI_UNSUPPORTED;
            high = dword_private(draft->Config,20+4*bar);
        }
        if ((low & 7) != 0 && (low & 7) != 4) return EFI_UNSUPPORTED;
        owner->Stage = 0x500+bar;
        status = pci->Bar(pci,(UINT8)bar,0,&resources);
        if (status == EFI_UNSUPPORTED) {
            owner->UnavailableBars |= 1U << bar;
            if ((low & 7) == 4) ++bar;
            continue;
        }
        if (status != EFI_SUCCESS) return status;
        owner->Resources = resources;
        owner->Stage = 6;
        if (!boot_vgpu_descriptor(owner->Resources,48,low,high,bits,&draft->Sizes[bar]))
            return EFI_UNSUPPORTED;
        status = owner->Services->Prefix.FreePool(owner->Resources);
        if (status != EFI_SUCCESS) return status;
        owner->Resources = 0;
        if ((low & 7) == 4) ++bar;
    }
    return EFI_SUCCESS;
}

// ====== capture_private ======

static EFI_STATUS capture_private(BOOT_PCI_IO *pci, UINT32 bits,
    BOOT_PCI_OWNER *owner, KERNEL_VGPU_BOOT *draft)
{
    UINTN segment, bus, device, function;
    unsigned char verify[256];
    EFI_STATUS status;
    if (!pci->Location || !pci->Bar) return EFI_UNSUPPORTED;
    owner->Stage = 4;
    status = pci->Location(pci,&segment,&bus,&device,&function);
    if (status != EFI_SUCCESS) return status;
    if (segment || bus > 255 || device > 31 || function > 7) return EFI_UNSUPPORTED;
    draft->Segment = (UINT32)segment; draft->Bus = (UINT32)bus;
    draft->Device = (UINT32)device; draft->Function = (UINT32)function;
    status = resources_private(pci,bits,owner,draft);
    if (status != EFI_SUCCESS) return status;
    owner->Stage = 7;
    status = snapshot_private(pci,verify);
    if (status != EFI_SUCCESS) return status;
    for (UINT32 byte = 0; byte < 256; ++byte)
        if (verify[byte] != draft->Config[byte]) return EFI_DEVICE_ERROR;
    draft->Present = 1;
    owner->Stage = 8;
    return kernel_vgpu_boot_valid(draft,bits) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

// ====== boot_vgpu_acquire ======

// UEFI2.10 7.3.15: ByProtocol=2; 14.4.8 Pci.Read width Uint8=0;
// 14.4.16/18 return location/resources. No PCI writes or Attributes changes.
EFI_STATUS boot_vgpu_acquire(EFI_SYSTEM_TABLE *table, UINT32 bits, BOOT_PCI_OWNER *owner)
{
    EFI_STATUS status, released;
    KERNEL_VGPU_BOOT candidate = {0};
    UINT32 matches = 0;
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    if (!owner || owner->Attempted || owner->Services || owner->Handles || owner->Resources ||
        owner->Valid || owner->Stage || owner->UnavailableBars || owner->HandleCount || !table || bits < 32 || bits > 52 ||
        table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE || table->Hdr.HeaderSize < sizeof(*table))
        return EFI_INVALID_PARAMETER;
    for (UINT32 byte = 0; byte < sizeof(owner->Result); ++byte)
        if (((unsigned char *)&owner->Result)[byte]) return EFI_INVALID_PARAMETER;
    owner->Services = table->BootServices;
    if (!owner->Services || owner->Services->Prefix.Hdr.HeaderSize < sizeof(*owner->Services) ||
        !owner->Services->Locate || !owner->Services->Prefix.HandleProtocol ||
        !owner->Services->Prefix.FreePool) { owner->Services = 0; return EFI_UNSUPPORTED; }
    owner->Attempted = 1;
    owner->Stage = 2;
    status = owner->Services->Locate(2,&pci_guid,0,&count,&handles);
    if (status != EFI_SUCCESS) goto done;
    owner->HandleCount = count; owner->Handles = handles;
    if (!owner->Handles || !owner->HandleCount || owner->HandleCount > 256) {
        status = EFI_UNSUPPORTED; goto done;
    }
    status = EFI_NOT_FOUND;
    for (UINTN index = 0; index < owner->HandleCount; ++index) {
        BOOT_PCI_IO *pci = 0;
        KERNEL_VGPU_BOOT draft = {0};
        EFI_STATUS current = owner->Services->Prefix.HandleProtocol(owner->Handles[index],&pci_guid,(void **)&pci);
        if (current != EFI_SUCCESS || !pci || !pci->Read) { status = EFI_DEVICE_ERROR; break; }
        owner->Stage = 3;
        current = pci->Read(pci,0,0,4,draft.Config);
        if (current != EFI_SUCCESS) { status = current; break; }
        if (dword_private(draft.Config,0) != 0x10501af4U) continue;
        if (++matches != 1) { status = EFI_UNSUPPORTED; break; }
        current=snapshot_private(pci,draft.Config);
        if (current != EFI_SUCCESS) { status=current; break; }
        status = capture_private(pci,bits,owner,&draft);
        if (status != EFI_SUCCESS) break;
        candidate = draft;
    }
done:
    released = boot_vgpu_release(owner);
    if (released != EFI_SUCCESS) return released;
    if (status == EFI_SUCCESS && matches == 1) {
        owner->Result = candidate;
        owner->Valid = 1;
    }
    return status;
}
