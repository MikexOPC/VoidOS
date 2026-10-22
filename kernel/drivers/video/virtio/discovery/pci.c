#include "pci.h"
#include "lib/memory/address_ranges.h"

// ====== little_private ======

static uint32_t little_private(const unsigned char *bytes, uint32_t offset, uint32_t count)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < count; ++index)
        value |= (uint32_t)bytes[offset+index] << (index*8);
    return value;
}

// ====== bars_private ======

// PCI header BAR0=10h, memory type bits2:1=00/10 for 32/64-bit.
// Linux include/uapi/linux/pci_regs.h, PCI_BASE_ADDRESS_*; Virtio1.2 4.1.4.
static void bars_private(const unsigned char *config, const uint64_t *sizes,
    uint32_t bits, uint64_t bases[6])
{
    for (uint32_t index = 0; index < 6; ++index) bases[index] = UINT64_MAX;
    for (uint32_t index = 0; index < 6; ++index) {
        uint32_t low = little_private(config,0x10+index*4,4), high = 0;
        if ((low & 7) == 4) {
            if (index == 5) continue;
            high = little_private(config,0x14+index*4,4);
        }
        bases[index] = kernel_vgpu_pci_bar(low,high,sizes[index],bits);
        if ((low & 7) == 4 && index < 5) ++index;
    }
}

// ====== capability_private ======

// Virtio1.2 4.1.4: vendor09, length+2, type+3, BAR+4, offset+8,
// region length+12; 4.1.4.4 adds notify multiplier+16.
static uint32_t capability_private(const unsigned char *config, uint32_t cursor,
    uint32_t length, const uint64_t *bases, const uint64_t *sizes,
    KERNEL_VGPU_PCI_LAYOUT *draft, uint32_t *found)
{
    uint32_t kind, bar, offset, span, multiplier = 0;
    uint64_t address;
    if (length < 4) return KERNEL_VGPU_PCI_REJECTED;
    kind = config[cursor+3];
    if (kind < 1 || kind > 4) return KERNEL_VGPU_PCI_READY;
    if (length < (kind == 2 ? 20U : 16U)) return KERNEL_VGPU_PCI_REJECTED;
    bar = config[cursor+4];
    if (bar > 5) return KERNEL_VGPU_PCI_REJECTED;
    if (bases[bar] == UINT64_MAX) return KERNEL_VGPU_PCI_READY;
    offset = little_private(config,cursor+8,4);
    span = little_private(config,cursor+12,4);
    if (kind == 2) multiplier = little_private(config,cursor+16,4);
    address = kernel_vgpu_pci_region(kind,offset,span,multiplier,bases[bar],sizes[bar]);
    if (address == UINT64_MAX) return KERNEL_VGPU_PCI_REJECTED;
    if (!(*found & (1U << (kind-1)))) {
        draft->Regions[kind-1] = (KERNEL_VGPU_PCI_REGION){address,span,bar,offset,multiplier,0};
        *found |= 1U << (kind-1);
    }
    return KERNEL_VGPU_PCI_READY;
}

// ====== kernel_vgpu_pci_probe ======

// PCI header: vendor/device00h/02h, status06h bit4, type0Eh,
// capability pointer34h (Linux pci_regs.h). Virtio1.2 4.1.2: 1AF4:1050.
uint32_t kernel_vgpu_pci_probe(const unsigned char *config, uint32_t bytes,
    const uint64_t *sizes, uint32_t size_count, uint32_t physical_bits,
    KERNEL_VGPU_PCI_LAYOUT *output)
{
    KERNEL_VGPU_PCI_LAYOUT draft = {0};
    uint64_t bases[6], visited = 0;
    unsigned char occupied[256] = {0};
    uint32_t cursor, found = 0;
    if (!config || !sizes || !output || bytes != 256 || size_count != 6 ||
        ((uintptr_t)sizes & 7) || ((uintptr_t)output & 7) || physical_bits < 32 || physical_bits > 52 ||
        (uintptr_t)config > UINTPTR_MAX-256 || (uintptr_t)sizes > UINTPTR_MAX-48 ||
        (uintptr_t)output > UINTPTR_MAX-sizeof(*output) ||
        os_address_ranges_overlap((uintptr_t)config,256,(uintptr_t)output,sizeof(*output)) ||
        os_address_ranges_overlap((uintptr_t)sizes,48,(uintptr_t)output,sizeof(*output)))
        return KERNEL_VGPU_PCI_REJECTED;
    if (little_private(config,0,4) != 0x10501af4U) return KERNEL_VGPU_PCI_ABSENT;
    if ((config[0x0e] & 127) || config[0x0b] != 3 ||
        (config[0x0a] != 0 && config[0x0a] != 0x80) || !(config[6] & 16))
        return KERNEL_VGPU_PCI_UNSUPPORTED;
    bars_private(config,sizes,physical_bits,bases);
    draft.Revision = config[8]; draft.Class = little_private(config,9,3);
    cursor = config[0x34];
    while (cursor) {
        uint32_t length = 2;
        if (cursor < 0x40 || cursor > 0xfc || (cursor & 3) ||
            (visited & (UINT64_C(1) << (cursor/4)))) return KERNEL_VGPU_PCI_REJECTED;
        visited |= UINT64_C(1) << (cursor/4);
        if (++draft.Capabilities > 48) return KERNEL_VGPU_PCI_REJECTED;
        if (config[cursor] == 9) {
            length = config[cursor+2];
            if (length < 3 || length > 256-cursor) return KERNEL_VGPU_PCI_REJECTED;
        }
        for (uint32_t index = 0; index < length; ++index) {
            if (occupied[cursor+index]) return KERNEL_VGPU_PCI_REJECTED;
            occupied[cursor+index] = 1;
        }
        if (config[cursor] == 9 && capability_private(config,cursor,length,bases,sizes,&draft,&found)
            != KERNEL_VGPU_PCI_READY) return KERNEL_VGPU_PCI_REJECTED;
        cursor = config[cursor+1];
    }
    if (found != 15) return KERNEL_VGPU_PCI_UNSUPPORTED;
    for (uint32_t first = 0; first < 4; ++first) for (uint32_t second = first+1; second < 4; ++second)
        if (os_address_ranges_overlap(draft.Regions[first].Address,draft.Regions[first].Length,
            draft.Regions[second].Address,draft.Regions[second].Length)) return KERNEL_VGPU_PCI_REJECTED;
    *output = draft;
    return KERNEL_VGPU_PCI_READY;
}
