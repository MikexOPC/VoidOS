#include "native.h"
#include <intrin.h>
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/mm/devices/lapic.h"
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

#define X86_AHCI_VECTOR 0x45U

typedef struct {
    uintptr_t                  Self;
    X86_PAGE_WINDOW           *Window;
    const X86_LAPIC_MAPPING   *LapicMapping;
    X86_PM_CLOCK               Clock;
    uint64_t                   UcCacheBits;
    uint64_t                   EcamRangeBase;
    AHCI_PCI_LAYOUT            Layout;
    uint32_t                   Segment;
    uint32_t                   Bus;
    uint32_t                   Device;
    uint32_t                   Function;
    uint32_t                   Present;
    uint32_t                   Ready;
    uint32_t                   IrqEnabled;
    uint32_t                   MsiCapOffset;
    X86_IRQ_TOKEN              IrqToken;
    uint64_t                   IrqSeq;
    KERNEL_LOCK                DeviceLock;
    KERNEL_AHCI_DEVICE         Driver;
    KERNEL_BLOCK_CACHE         Cache;
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

// ====== transport_irq_seq_private ======

static uint64_t transport_irq_seq_private(void *context)
{
    const X86_AHCI_RUNTIME *rt = (const X86_AHCI_RUNTIME *)context;
    return rt ? ks_atomic_load_acquire(&rt->IrqSeq) : 0ULL;
}

// ====== transport_wait_private ======

static void transport_wait_private(void *context, uint64_t expected_seq)
{
    const X86_AHCI_RUNTIME *rt = (const X86_AHCI_RUNTIME *)context;
    if (!rt || !rt->IrqEnabled || !ks_irq_state()) {
        _mm_pause();
        return;
    }
    // Intel SDM Vol. 2B STI: IF becomes 1 after the following HLT instruction (atomic sleep, no lost wakeup).
    uint64_t saved_if = ks_irq_save();
    if (ks_atomic_load_acquire(&rt->IrqSeq) == expected_seq) {
        _enable();
        __halt();
    } else {
        ks_irq_restore(saved_if);
    }
}

// ====== ahci_irq_handle_private ======

static int ahci_irq_handle_private(void)
{
    if (!ahci_rt.LapicMapping || !x86_lapic_eoi_vector(ahci_rt.LapicMapping, X86_AHCI_VECTOR))
        return 0;
    ks_atomic_store_release(&ahci_rt.IrqSeq, ks_atomic_load_relaxed(&ahci_rt.IrqSeq) + 1ULL);
    return 1;
}

// ====== ahci_irq_quiet_private ======

static int ahci_irq_quiet_private(void)
{
    return 1;
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
                        ahci_rt.EcamRangeBase = range->Base;
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

// ====== enable_msi_private ======

// PCI Local Bus Spec 3.0 §6.8.1 (MSI Cap 0x05) & Intel SDM Vol. 3A §11.11 (LAPIC MSI address 0xFEE00000).
static int enable_msi_private(const uint8_t pci_cfg[256], uint32_t bsp_apic_id)
{
    if (!(pci_cfg[0x06U] & 0x10U) || !ahci_rt.EcamRangeBase || !x86_irq_registry())
        return 0;
    uint32_t cap = pci_cfg[0x34U] & 0xFCU;
    uint32_t msi_cap = 0U;
    for (uint32_t steps = 0; cap >= 0x40U && cap <= 0xFCU && steps < 48U; ++steps) {
        if (pci_cfg[cap] == 0x05U) {
            msi_cap = cap;
            break;
        }
        cap = pci_cfg[cap + 1U] & 0xFCU;
    }
    if (!msi_cap || msi_cap + 12U > 256U)
        return 0;

    if (!x86_irq_registry_bind(x86_irq_registry(), X86_AHCI_VECTOR,
            ahci_irq_handle_private, ahci_irq_quiet_private, &ahci_rt.IrqToken))
        return 0;

    uint32_t ctrl = 0, readback = 0, cmd = 0;
    if (!ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 2U, 2U, 0, &ctrl)) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &ahci_rt.IrqToken);
        return 0;
    }
    int is_64 = (ctrl & 0x0080U) != 0U;
    uint32_t addr_lo = 0xFEE00000U | ((bsp_apic_id & 0xFFU) << 12);
    uint32_t addr_hi = 0U;
    uint32_t data = X86_AHCI_VECTOR;
    int ok = ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
        ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 4U, 4U, 1, &addr_lo);
    if (ok && is_64) {
        ok = ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 8U, 4U, 1, &addr_hi) &&
            ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 12U, 2U, 1, &data);
    } else if (ok) {
        ok = ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 8U, 2U, 1, &data);
    }
    if (!ok) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &ahci_rt.IrqToken);
        return 0;
    }

    // Multiple Message Enable = 000 (1 vector), MSI Enable (bit 0) = 1.
    ctrl = (ctrl & ~0x0070U) | 0x0001U;
    if (!ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 2U, 2U, 1, &ctrl) ||
        !ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, msi_cap + 2U, 2U, 0, &readback) ||
        (readback & 0x0001U) == 0U) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &ahci_rt.IrqToken);
        return 0;
    }
    if (ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, 0x04U, 2U, 0, &cmd)) {
        cmd |= 0x0400U;
        (void)ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
            ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, 0x04U, 2U, 1, &cmd);
    }
    ahci_rt.MsiCapOffset = msi_cap;
    return 1;
}

// ====== x86_ahci_stop ======

void x86_ahci_stop(void)
{
    if (!ahci_rt.IrqEnabled)
        return;
    if (ahci_rt.MsiCapOffset && ahci_rt.EcamRangeBase) {
        uint32_t ctrl = 0;
        if (ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
                ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, ahci_rt.MsiCapOffset + 2U, 2U, 0, &ctrl)) {
            ctrl &= ~0x0001U;
            (void)ecam_rw_private(ahci_rt.Window, ahci_rt.UcCacheBits, ahci_rt.EcamRangeBase,
                ahci_rt.Bus, ahci_rt.Device, ahci_rt.Function, ahci_rt.MsiCapOffset + 2U, 2U, 1, &ctrl);
        }
    }
    if (ahci_rt.IrqToken.Id && x86_irq_registry())
        (void)x86_irq_registry_unbind(x86_irq_registry(), &ahci_rt.IrqToken);
    ahci_rt.IrqEnabled = 0U;
    ahci_rt.Driver.Transport.IrqEnabled = 0U;
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

    uint64_t control_phys = 0ULL;
    uint64_t data_phys = 0ULL;
    if (!x86_heap_virt_to_phys((uintptr_t)dma_pages, &control_phys) ||
        !x86_heap_virt_to_phys((uintptr_t)(dma_pages + 4096U), &data_phys))
        return 0;

    if (!kernel_lock_initialize(&ahci_rt.DeviceLock, KERNEL_BLOCK_DEVICE_LOCK_RANK))
        return 0;

    ahci_rt.Window = env->Window;
    ahci_rt.LapicMapping = env->Mapping;
    ahci_rt.Clock = *clock;
    ahci_rt.UcCacheBits = uc_cache;
    ahci_rt.Present = 1U;
    ahci_rt.Self = (uintptr_t)&ahci_rt;

    BOOT_U32 lapic_id_reg = 0;
    uint32_t bsp_apic_id = (env->Mapping && x86_lapic_read(env->Mapping, 0x020U, &lapic_id_reg))
        ? (lapic_id_reg >> 24)
        : 0U;
    ahci_rt.IrqEnabled = enable_msi_private(pci_cfg, bsp_apic_id) ? 1U : 0U;

    AHCI_TRANSPORT transport = {
        &ahci_rt,
        transport_mmio_private,
        transport_clock_private,
        transport_barrier_private,
        clock->Mask,
        3579545U,
        1000000U,
        transport_wait_private,
        transport_irq_seq_private,
        ahci_rt.IrqEnabled
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
        kernel_text("\nKERNEL_AHCI_IRQ: ") && kernel_number(ahci_rt.IrqEnabled ? X86_AHCI_VECTOR : 0ULL) &&
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
