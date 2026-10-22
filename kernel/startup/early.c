#include "startup.h"
#include "../drivers/video/framebuffer/handoff.h"
#include "../drivers/video/framebuffer/mapping.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "../diagnostics/log.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== contract_private ======

// A1-A2: boot contract v5 and the GPU PCI identity carried by the loader.
static void contract_private(KERNEL_BOOTSTRAP_MEMORY *memory, const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    if (!kernel_capture_contract(info, root, entry, &memory->Context)) kernel_panic("BOOT_CONTRACT_INVALID");
    if (!kernel_log(0, "KERNEL_CONTRACT_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_log(0,memory->Context.Info.Gpu.Present ? "KERNEL_VGPU_PCI_PRESENT" : "KERNEL_VGPU_PCI_ABSENT"))
        kernel_panic("SERIAL_UNAVAILABLE");
    if (memory->Context.Info.Gpu.Present) {
        KERNEL_VGPU_PCI_LAYOUT layout;
        const KERNEL_VGPU_BOOT *gpu = &memory->Context.Info.Gpu;
        if (kernel_vgpu_pci_probe(gpu->Config,256,gpu->Sizes,6,52,&layout) != KERNEL_VGPU_PCI_READY)
            kernel_panic("VGPU_METADATA_INVALID");
        for (BOOT_U32 region = 0; region < 4; ++region)
            if (!kernel_text("KERNEL_VGPU_REGION: ") || !kernel_number(layout.Regions[region].Address) ||
                !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    }
}

// ====== framebuffer_private ======

// A5: framebuffer handoff checked against the memory map and reported.
static void framebuffer_private(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    if (!fb_handoff_memory_valid(&memory->Context.Info.Framebuffer,memory->Context.MemoryMap,
        memory->Context.Info.MemoryMapBytes,memory->Context.Info.MemoryDescriptorBytes,52))
        kernel_panic("FRAMEBUFFER_RESOURCES_INVALID");
    if (!kernel_log(0,memory->Context.Info.Framebuffer.Base ?
        "KERNEL_FRAMEBUFFER_PRESENT" : "KERNEL_FRAMEBUFFER_ABSENT")) kernel_panic("SERIAL_UNAVAILABLE");
    const OS_FRAMEBUFFER *fb = &memory->Context.Info.Framebuffer;
    if (!kernel_text("KERNEL_BOOT_VERSION: ") || !kernel_number(memory->Context.Info.Version) ||
        !kernel_text("\nKERNEL_BOOT_SIZE: ") || !kernel_number(memory->Context.Info.Size) ||
        !kernel_text("\nKERNEL_FB_BASE: ") || !kernel_number(fb->Base) ||
        !kernel_text("\nKERNEL_FB_CAPACITY: ") || !kernel_number(fb->Capacity) ||
        !kernel_text("\nKERNEL_FB_USED: ") || !kernel_number(fb->Used) ||
        !kernel_text("\nKERNEL_FB_WIDTH: ") || !kernel_number(fb->Width) ||
        !kernel_text("\nKERNEL_FB_HEIGHT: ") || !kernel_number(fb->Height) ||
        !kernel_text("\nKERNEL_FB_STRIDE: ") || !kernel_number(fb->Stride) ||
        !kernel_text("\nKERNEL_FB_FORMAT: ") || !kernel_number(fb->Format) ||
        !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== plan_private ======

// A7: framebuffer page-table plan, no allocation.
static void plan_private(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    FB_MAPPING_GEOMETRY geometry;
    BOOT_U64 budget = memory->Physical.FreePages;
    if (!fb_mapping_geometry(&memory->Context.Info.Framebuffer,memory->Physical.PhysicalBits,&geometry) ||
        geometry.TablePages > memory->Physical.FreePages || memory->Physical.FreePages != budget)
        kernel_panic("FRAMEBUFFER_PLAN_INVALID");
    if (!kernel_text("KERNEL_FB_PLAN_PAGES: ") || !kernel_number(geometry.Pages) ||
        !kernel_text("\nKERNEL_FB_PLAN_TABLES: ") || !kernel_number(geometry.TablePages) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_FRAMEBUFFER_PLAN_OK"))
        kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== kernel_startup_early ======

// Phase A on the loader stack: A1-A7 (descriptors and the main stack stay in main.c).
void kernel_startup_early(KERNEL_BOOTSTRAP_MEMORY *memory, const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    contract_private(memory, info, root, entry);

    if (!kernel_section("IDT eccezioni kernel") || !x86_exception_initialize()) kernel_panic("KERNEL_IDT_FAILED");
    kernel_section("inventario memoria");

    if (!kernel_inventory_memory(memory->Context.MemoryMap, memory->Context.Info.MemoryMapBytes, memory->Context.Info.MemoryDescriptorBytes, &memory->Inventory)
    )
    kernel_panic("MEMORY_MAP_INVALID");

    framebuffer_private(memory);

    if (!kernel_log(0, "KERNEL_MEMORY_OK") || !kernel_text("Pagine convenzionali candidate: ") ||
        !kernel_number(memory->Inventory.ConventionalPages) || !kernel_text("\nPagine riservate: ") ||
        !kernel_number(memory->Inventory.ReservedPages) || !kernel_text("\n")
    ) 
    kernel_panic("SERIAL_UNAVAILABLE");

    kernel_bootstrap_memory_prepare(memory);
    
    plan_private(memory);
}
