#include "native.h"
#include <intrin.h>
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/mm/paging/heap/authority.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"

typedef struct {
    X86_PAGE_WINDOW            *Window;
    const KERNEL_BOOT_CONTEXT  *Context;
} ACPI_READ_CTX;

typedef struct {
    uint32_t  InPageOffset;
    uint32_t  Width;
    int       Write;
    uint32_t  Value;
} MMIO_BORROW_REQ;

typedef struct {
    uintptr_t           Self;
    X86_PAGE_WINDOW    *Window;
    X86_PM_CLOCK        Clock;
    uint64_t            UcCacheBits;
    AHCI_PCI_LAYOUT     Layout;
    uint32_t            Segment;
    uint32_t            Bus;
    uint32_t            Device;
    uint32_t            Function;
    uint32_t            Present;
    uint32_t            Ready;
    KERNEL_LOCK         DeviceLock;
    KERNEL_AHCI_DEVICE  Driver;
    KERNEL_BLOCK_CACHE  Cache;
} X86_AHCI_RUNTIME;

static X86_AHCI_RUNTIME ahci_rt;
static unsigned char ahci_mcfg_scratch[X86_MCFG_BYTES];
static X86_MCFG ahci_mcfg;

// ====== acpi_read_private ======

static int acpi_read_private(void *state, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    const ACPI_READ_CTX *ctx = (const ACPI_READ_CTX *)state;
    return x86_page_window_read_acpi(ctx->Window, ctx->Context, physical, bytes, output);
}

// ====== mmio_window_cb ======

static int mmio_window_cb(void *opaque)
{
    MMIO_BORROW_REQ *req = (MMIO_BORROW_REQ *)opaque;
    volatile uint8_t *base = (volatile uint8_t *)x86_window_bytes();
    if (!base)
        return 0;

    volatile uint8_t *ptr = base + req->InPageOffset;
    _ReadWriteBarrier();
    if (req->Width == 2U) {
        volatile uint16_t *reg16 = (volatile uint16_t *)ptr;
        if (req->Write)
            *reg16 = (uint16_t)req->Value;
        else
            req->Value = *reg16;
    } else if (req->Width == 4U) {
        volatile uint32_t *reg32 = (volatile uint32_t *)ptr;
        if (req->Write)
            *reg32 = req->Value;
        else
            req->Value = *reg32;
    } else {
        return 0;
    }
    _ReadWriteBarrier();
    return 1;
}

// ====== transport_mmio_private ======

static int transport_mmio_private(void *context, uint32_t offset, int write, uint32_t *value)
{
    X86_AHCI_RUNTIME *rt = (X86_AHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt || !value || (offset & 3U) || offset + 4U > 0x1100U)
        return 0;

    uint64_t phys_addr = rt->Layout.AbarAddress + offset;
    uint64_t page_phys = phys_addr & ~4095ULL;
    uint32_t in_page = (uint32_t)(phys_addr & 4095ULL);
    if (in_page + 4U > 4096U)
        return 0;

    MMIO_BORROW_REQ req = {in_page, 4U, write != 0, write ? *value : 0U};
    uint64_t saved_if = ks_irq_save();
    int ok = x86_apic_borrow(rt->Window, page_phys, rt->UcCacheBits, write != 0, mmio_window_cb, &req);
    ks_irq_restore(saved_if);
    if (!ok)
        return 0;

    if (!write)
        *value = req.Value;
    return 1;
}

// ====== transport_clock_private ======

static int transport_clock_private(void *context, uint32_t *ticks_out)
{
    const X86_AHCI_RUNTIME *rt = (const X86_AHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt || !ticks_out || !rt->Clock.Ready)
        return 0;
    *ticks_out = x86_pm_read((unsigned short)rt->Clock.Port) & rt->Clock.Mask;
    return 1;
}

// ====== transport_barrier_private ======

static void transport_barrier_private(void *context)
{
    (void)context;
    _ReadWriteBarrier();
    _mm_mfence();
    _ReadWriteBarrier();
}

// ====== ecam_rw_private ======

static int ecam_rw_private(
    X86_PAGE_WINDOW *window,
    uint64_t uc_cache,
    uint64_t range_base,
    uint32_t bus,
    uint32_t dev,
    uint32_t fn,
    uint32_t offset,
    uint32_t width,
    int write,
    uint32_t *value
)
{
    uint64_t page = x86_ecam_page(range_base, bus, dev, fn);
    if (!page || offset + width > 4096U || (offset & (width - 1U)))
        return 0;

    MMIO_BORROW_REQ req = {offset, width, write != 0, write ? *value : 0U};
    uint64_t saved_if = ks_irq_save();
    int ok = x86_apic_borrow(window, page, uc_cache, write != 0, mmio_window_cb, &req);
    ks_irq_restore(saved_if);
    if (!ok)
        return 0;
    if (!write)
        *value = req.Value;
    return 1;
}

// ====== discover_pci_private ======

static int discover_pci_private(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    uint64_t uc_cache,
    uint8_t config_out[256]
)
{
    ACPI_READ_CTX acpi_ctx = {env->Window, env->Context};
    uint32_t bits = env->Window->Pool->PhysicalBits;

    int found_mcfg = x86_mcfg_find(
        directory,
        bits,
        acpi_read_private,
        &acpi_ctx,
        ahci_mcfg_scratch,
        sizeof(ahci_mcfg_scratch),
        &ahci_mcfg
    );
    if (found_mcfg == 2)
        return 0;
    if (found_mcfg != 1)
        return -1;

    int found = 0;
    for (uint32_t r = 0; r < ahci_mcfg.Count && !found; ++r) {
        const X86_ECAM_RANGE *range = &ahci_mcfg.Ranges[r];
        uint32_t last_bus = range->Last > range->First + 3U ? range->First + 3U : range->Last;

        for (uint32_t bus = range->First; bus <= last_bus && !found; ++bus) {
            for (uint32_t dev = 0; dev < 32U && !found; ++dev) {
                for (uint32_t fn = 0; fn < 8U && !found; ++fn) {
                    uint32_t id = 0xFFFFFFFFU;
                    if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x00U, 4U, 0, &id))
                        continue;
                    if ((id & 0xFFFFU) == 0xFFFFU) {
                        if (fn == 0U)
                            break;
                        continue;
                    }

                    uint32_t class_rev = 0;
                    if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x08U, 4U, 0, &class_rev))
                        continue;

                    // Intel AHCI 1.3.1 §2.1.7: Class Code 01:06:01 (Serial ATA AHCI 1.0).
                    if ((class_rev >> 8) == 0x010601U) {
                        uint32_t cmd = 0;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 0, &cmd))
                            return -1;
                        cmd |= 0x06U;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 1, &cmd))
                            return -1;

                        for (uint32_t word = 0; word < 64U; ++word) {
                            uint32_t val = 0;
                            if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, word * 4U, 4U, 0, &val))
                                return -1;
                            config_out[word * 4U + 0U] = (uint8_t)(val & 0xFFU);
                            config_out[word * 4U + 1U] = (uint8_t)((val >> 8) & 0xFFU);
                            config_out[word * 4U + 2U] = (uint8_t)((val >> 16) & 0xFFU);
                            config_out[word * 4U + 3U] = (uint8_t)((val >> 24) & 0xFFU);
                        }
                        ahci_rt.Segment = range->Segment;
                        ahci_rt.Bus = bus;
                        ahci_rt.Device = dev;
                        ahci_rt.Function = fn;
                        found = 1;
                        break;
                    }

                    if (fn == 0U) {
                        uint32_t hdr = 0;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, 0U, 0x0CU, 4U, 0, &hdr) ||
                            !((hdr >> 16) & 0x80U))
                            break;
                    }
                }
            }
        }
    }

    return found;
}

// ====== verify_io_private ======

static int verify_io_private(KERNEL_BLOCK_CACHE *cache)
{
    uint8_t sector0[512];
    for (uint32_t i = 0; i < 512U; ++i)
        sector0[i] = 0;

    if (kernel_block_cache_read_sectors(cache, 0ULL, 1U, sector0) != KERNEL_STORAGE_OK)
        return 0;

    // Boot disk LBA 0 (MBR / FAT boot sector) must carry the standard 0x55, 0xAA signature.
    return sector0[510] == 0x55U && sector0[511] == 0xAAU;
}

// ====== x86_ahci_start ======

int x86_ahci_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
)
{
    if (ahci_rt.Ready)
        return 1;
    if (!env || !env->Context || !env->Window || !env->Window->Pool || !env->Inventory ||
        !directory || !clock || !clock->Ready)
        return 0;

    const X86_HEAP_AUTH *heap_auth = x86_heap_owner();
    KERNEL_HEAP *heap = kernel_heap();
    if (!heap_auth || !heap)
        return 0;

    X86_FB_CPU cpu;
    if (!x86_fb_cpu(&cpu) || !x86_apic_uc_slot(cpu.Pat))
        return 0;

    for (uint32_t i = 0; i < sizeof(ahci_rt); ++i)
        ((uint8_t *)&ahci_rt)[i] = 0;

    uint64_t uc_cache = x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat) - 1U);
    uint8_t pci_cfg[256];
    int found = discover_pci_private(env, directory, uc_cache, pci_cfg);
    if (found < 0)
        return 0;
    if (found == 0)
        return kernel_log(1, "KERNEL_AHCI_ABSENT");

    uint32_t probe = kahci_pci_probe(
        pci_cfg,
        256U,
        env->Window->Pool->PhysicalBits,
        &ahci_rt.Layout
    );
    if (probe != KAHCI_PCI_READY)
        return 0;

    uint8_t *dma_pages = (uint8_t *)kernel_heap_pages(heap, 2U);
    if (!dma_pages)
        return 0;

    uintptr_t dma_virt = (uintptr_t)dma_pages;
    if (dma_virt < X86_HEAP_VIRTUAL ||
        dma_virt + 8192ULL > X86_HEAP_VIRTUAL + (uint64_t)X86_HEAP_PAGES * 4096ULL)
        return 0;

    uint64_t control_phys = heap_auth->Data.Base + (dma_virt - X86_HEAP_VIRTUAL);
    uint64_t data_phys = control_phys + 4096ULL;

    if (!kernel_lock_initialize(&ahci_rt.DeviceLock, KERNEL_BLOCK_DEVICE_LOCK_RANK))
        return 0;

    ahci_rt.Window = env->Window;
    ahci_rt.Clock = *clock;
    ahci_rt.UcCacheBits = uc_cache;
    ahci_rt.Present = 1U;
    ahci_rt.Self = (uintptr_t)&ahci_rt;

    AHCI_TRANSPORT transport = {
        &ahci_rt,
        transport_mmio_private,
        transport_clock_private,
        transport_barrier_private,
        clock->Mask,
        3579545U,
        1000000U
    };

    int init_status = kernel_ahci_init(
        &ahci_rt.Driver,
        &transport,
        dma_pages,
        control_phys,
        dma_pages + 4096U,
        data_phys
    );
    if (init_status == KERNEL_STORAGE_ERR_NOT_FOUND)
        return kernel_log(1, "KERNEL_AHCI_NO_DRIVE");
    if (init_status != KERNEL_STORAGE_OK)
        return 0;

    if (kernel_block_cache_init(
            &ahci_rt.Cache,
            heap,
            &ahci_rt.Driver.BlockDevice,
            64U
        ) != KERNEL_STORAGE_OK)
        return 0;

    if (!verify_io_private(&ahci_rt.Cache))
        return 0;

    ahci_rt.Ready = 1U;
    uint64_t bdf = ((uint64_t)ahci_rt.Bus << 8) | ((uint64_t)ahci_rt.Device << 3) | ahci_rt.Function;
    return kernel_text("KERNEL_AHCI_BDF: ") && kernel_number(bdf) &&
        kernel_text("\nKERNEL_AHCI_PORT: ") && kernel_number(ahci_rt.Driver.PortIndex) &&
        kernel_text("\nKERNEL_AHCI_SECTORS: ") && kernel_number(ahci_rt.Driver.CapacitySectors) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_AHCI_READY");
}

// ====== x86_ahci_device ======

KERNEL_BLOCK_DEVICE *x86_ahci_device(void)
{
    return (ahci_rt.Self == (uintptr_t)&ahci_rt && ahci_rt.Ready) ? &ahci_rt.Driver.BlockDevice : NULL;
}

// ====== x86_ahci_cache ======

KERNEL_BLOCK_CACHE *x86_ahci_cache(void)
{
    return (ahci_rt.Self == (uintptr_t)&ahci_rt && ahci_rt.Ready) ? &ahci_rt.Cache : NULL;
}
