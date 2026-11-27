#include "native.h"
#include <intrin.h>
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"

// MSI-X vector 0x48 (within 0x44..0x4F allowed by x86_lapic_idle_except).
#define X86_NVME_VECTOR 0x48U

typedef struct {
    X86_PAGE_WINDOW            *Window;
    const X86_LAPIC_MAPPING    *LapicMapping;
    X86_PM_CLOCK                Clock;
    uint64_t                    UcCacheBits;
    uint64_t                    Bar0Phys;
    uint32_t                    Bus;
    uint32_t                    Device;
    uint32_t                    Function;
    uint32_t                    Present;
    uint32_t                    Ready;
    uint32_t                    IrqEnabled;
    uint32_t                    TargetApicId;
    X86_IRQ_TOKEN               IrqToken;
    uint64_t                    IrqSeq;
    KERNEL_NVME_DRIVER          Driver;
    KERNEL_BLOCK_CACHE          Cache;
    uintptr_t                   Self;
} X86_NVME_RUNTIME;

static X86_NVME_RUNTIME nvme_rt;
static X86_MCFG nvme_mcfg;
static unsigned char nvme_mcfg_scratch[4096];

typedef struct {
    X86_PAGE_WINDOW            *Window;
    const KERNEL_BOOT_CONTEXT  *Context;
} ACPI_READ_CTX;

typedef struct {
    uint32_t  Offset;
    uint32_t  Width;
    int       Write;
    uint64_t  Value;
} MMIO_BORROW_REQ;

// ====== acpi_read_private ======

static int acpi_read_private(void *opaque, OS_ACPI_U64 address, OS_ACPI_U32 bytes, unsigned char *output)
{
    const ACPI_READ_CTX *ctx = (const ACPI_READ_CTX *)opaque;
    if (!ctx || !ctx->Window || !ctx->Context)
        return 0;
    return x86_page_window_read_acpi(ctx->Window, ctx->Context, address, bytes, output);
}

// ====== mmio_window_cb ======

static int mmio_window_cb(void *opaque)
{
    MMIO_BORROW_REQ *req = (MMIO_BORROW_REQ *)opaque;
    volatile uint8_t *base = (volatile uint8_t *)x86_window_bytes();
    if (!base || !req || req->Offset + req->Width > 4096U)
        return 0;

    volatile uint8_t *ptr = base + req->Offset;
    if (req->Write) {
        if (req->Width == 1U)
            *ptr = (uint8_t)req->Value;
        else if (req->Width == 2U)
            *(volatile uint16_t *)ptr = (uint16_t)req->Value;
        else if (req->Width == 4U)
            *(volatile uint32_t *)ptr = (uint32_t)req->Value;
        else if (req->Width == 8U)
            *(volatile uint64_t *)ptr = req->Value;
        else
            return 0;
    } else {
        if (req->Width == 1U)
            req->Value = *ptr;
        else if (req->Width == 2U)
            req->Value = *(volatile uint16_t *)ptr;
        else if (req->Width == 4U)
            req->Value = *(volatile uint32_t *)ptr;
        else if (req->Width == 8U)
            req->Value = *(volatile uint64_t *)ptr;
        else
            return 0;
    }
    return 1;
}

// ====== mmio_phys_rw_private ======

static int mmio_phys_rw_private(
    X86_PAGE_WINDOW *window,
    uint64_t uc_cache,
    uint64_t phys_addr,
    uint32_t width,
    int write,
    uint64_t *value
)
{
    if (!window || !value || (phys_addr & (width - 1ULL)))
        return 0;
    uint64_t page_phys = phys_addr & ~4095ULL;
    uint32_t in_page = (uint32_t)(phys_addr & 4095ULL);
    if (in_page + width > 4096U)
        return 0;

    MMIO_BORROW_REQ req = {in_page, width, write != 0, write ? *value : 0ULL};
    uint64_t saved_if = ks_irq_save();
    int ok = x86_apic_borrow(window, page_phys, uc_cache, write != 0, mmio_window_cb, &req);
    ks_irq_restore(saved_if);
    if (!ok)
        return 0;
    if (!write)
        *value = req.Value;
    return 1;
}

// ====== transport_callbacks ======

static int transport_r32_private(void *ctx, uint32_t off, uint32_t *out)
{
    X86_NVME_RUNTIME *rt = (X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !out || !rt->Bar0Phys)
        return 0;
    uint64_t v = 0ULL;
    if (!mmio_phys_rw_private(rt->Window, rt->UcCacheBits, rt->Bar0Phys + off, 4U, 0, &v))
        return 0;
    *out = (uint32_t)v;
    return 1;
}

static int transport_w32_private(void *ctx, uint32_t off, uint32_t val)
{
    X86_NVME_RUNTIME *rt = (X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->Bar0Phys)
        return 0;
    uint64_t v = val;
    return mmio_phys_rw_private(rt->Window, rt->UcCacheBits, rt->Bar0Phys + off, 4U, 1, &v);
}

static int transport_r64_private(void *ctx, uint32_t off, uint64_t *out)
{
    X86_NVME_RUNTIME *rt = (X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !out || !rt->Bar0Phys)
        return 0;
    return mmio_phys_rw_private(rt->Window, rt->UcCacheBits, rt->Bar0Phys + off, 8U, 0, out);
}

static int transport_w64_private(void *ctx, uint32_t off, uint64_t val)
{
    X86_NVME_RUNTIME *rt = (X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->Bar0Phys)
        return 0;
    return mmio_phys_rw_private(rt->Window, rt->UcCacheBits, rt->Bar0Phys + off, 8U, 1, &val);
}

static int transport_clock_private(void *ctx, uint32_t *ticks_out)
{
    const X86_NVME_RUNTIME *rt = (const X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !ticks_out || !rt->Clock.Ready)
        return 0;
    *ticks_out = x86_pm_read((unsigned short)rt->Clock.Port) & rt->Clock.Mask;
    return 1;
}

static void transport_barrier_private(void *ctx)
{
    (void)ctx;
    _ReadWriteBarrier();
    _mm_mfence();
    _ReadWriteBarrier();
}

static uint64_t transport_irq_seq_private(void *ctx)
{
    const X86_NVME_RUNTIME *rt = (const X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt)
        return 0ULL;
    return ks_atomic_load_acquire(&rt->IrqSeq);
}

static void transport_wait_private(void *ctx, uint64_t expected_seq)
{
    const X86_NVME_RUNTIME *rt = (const X86_NVME_RUNTIME *)ctx;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->IrqEnabled || !ks_irq_state()) {
        _mm_pause();
        return;
    }
    if (ks_atomic_load_acquire(&rt->IrqSeq) >= expected_seq)
        return;
    // Intel SDM Vol. 3A §11.4.6: LAPIC ID Register at 0x20 (bits 31:24 in xAPIC).
    // Only halt on the CPU that receives the MSI-X vector; APs must spin with PAUSE.
    BOOT_U32 cur_apic = 0U;
    if (!rt->LapicMapping || !x86_lapic_read(rt->LapicMapping, 0x20U, &cur_apic) ||
        (cur_apic >> 24) != rt->TargetApicId) {
        _mm_pause();
        return;
    }
    uint64_t saved_if = ks_irq_save();
    if (ks_atomic_load_acquire(&rt->IrqSeq) < expected_seq) {
        _enable();
        __halt();
    } else {
        ks_irq_restore(saved_if);
    }
}

// ====== nvme_irq_handle ======

static int nvme_irq_handle(void)
{
    if (nvme_rt.Self != (uintptr_t)&nvme_rt || !nvme_rt.Present)
        return 0;
    if (!nvme_rt.LapicMapping || !x86_lapic_eoi_vector(nvme_rt.LapicMapping, X86_NVME_VECTOR))
        return 0;
    ks_atomic_store_release(&nvme_rt.IrqSeq, ks_atomic_load_relaxed(&nvme_rt.IrqSeq) + 1ULL);
    return 1;
}

static int nvme_irq_quiet(void)
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
    uint64_t v = write ? *value : 0ULL;
    if (!mmio_phys_rw_private(window, uc_cache, page + offset, width, write, &v))
        return 0;
    if (!write)
        *value = (uint32_t)v;
    return 1;
}

// ====== discover_pci_private ======

// PCI Local Bus Spec 3.0 & NVMe Base Spec Rev. 2.0c §2.1: Class 01h, Subclass 08h, ProgIF 02h.
static int discover_pci_private(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    uint64_t uc_cache,
    uint64_t *ecam_range_out,
    uint8_t cfg_out[256]
)
{
    ACPI_READ_CTX acpi_ctx = {env->Window, env->Context};
    uint32_t bits = env->Window->Pool->PhysicalBits;
    int found_mcfg = x86_mcfg_find(
        directory, bits, acpi_read_private, &acpi_ctx,
        nvme_mcfg_scratch, sizeof(nvme_mcfg_scratch), &nvme_mcfg
    );
    if (found_mcfg == 2)
        return 0;
    if (found_mcfg != 1)
        return -1;

    for (uint32_t r = 0; r < nvme_mcfg.Count; ++r) {
        const X86_ECAM_RANGE *range = &nvme_mcfg.Ranges[r];
        uint32_t last_bus = range->Last > range->First + 3U ? range->First + 3U : range->Last;
        for (uint32_t bus = range->First; bus <= last_bus; ++bus) {
            for (uint32_t dev = 0; dev < 32U; ++dev) {
                for (uint32_t fn = 0; fn < 8U; ++fn) {
                    uint32_t id = 0xFFFFFFFFU;
                    if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x00U, 4U, 0, &id))
                        continue;
                    if ((id & 0xFFFFU) == 0xFFFFU) {
                        if (fn == 0U)
                            break;
                        continue;
                    }
                    uint32_t class_reg = 0U;
                    if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x08U, 4U, 0, &class_reg))
                        continue;
                    uint8_t base_class = (uint8_t)(class_reg >> 24);
                    uint8_t sub_class = (uint8_t)((class_reg >> 16) & 0xFFU);
                    uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFFU);
                    if (base_class == 0x01U && sub_class == 0x08U && prog_if == 0x02U) {
                        uint32_t cmd = 0U;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 0, &cmd))
                            return -1;
                        cmd |= 0x06U; // Memory Space + Bus Master
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 1, &cmd))
                            return -1;
                        for (uint32_t w = 0; w < 64U; ++w) {
                            uint32_t val = 0U;
                            if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, w * 4U, 4U, 0, &val))
                                return -1;
                            cfg_out[w * 4U + 0U] = (uint8_t)(val & 0xFFU);
                            cfg_out[w * 4U + 1U] = (uint8_t)((val >> 8) & 0xFFU);
                            cfg_out[w * 4U + 2U] = (uint8_t)((val >> 16) & 0xFFU);
                            cfg_out[w * 4U + 3U] = (uint8_t)((val >> 24) & 0xFFU);
                        }
                        nvme_rt.Bus = bus;
                        nvme_rt.Device = dev;
                        nvme_rt.Function = fn;
                        *ecam_range_out = range->Base;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

// ====== enable_msix_private ======

static int enable_msix_private(uint64_t range_base, const uint8_t cfg[256], uint32_t apic_id)
{
    if ((cfg[0x06U] & 0x10U) == 0U)
        return 0;
    uint8_t cap = cfg[0x34U] & 0xFCU;
    for (uint32_t hops = 0; cap >= 0x40U && cap <= 0xF4U && hops < 48U; ++hops) {
        if (cfg[cap] == 0x11U) {
            uint32_t tbl = (uint32_t)cfg[cap + 4U]
                | ((uint32_t)cfg[cap + 5U] << 8)
                | ((uint32_t)cfg[cap + 6U] << 16)
                | ((uint32_t)cfg[cap + 7U] << 24);
            uint32_t bir = tbl & 7U;
            uint32_t off = tbl & ~7U;
            if (bir != 0U || !nvme_rt.Bar0Phys)
                return 0;
            uint64_t entry_phys = nvme_rt.Bar0Phys + off;
            uint64_t addr_lo = 0xFEE00000ULL | ((uint64_t)(apic_id & 0xFFU) << 12);
            uint64_t addr_hi = 0ULL;
            uint64_t data = X86_NVME_VECTOR;
            uint64_t ctrl = 0ULL;
            if (!x86_irq_registry_bind(x86_irq_registry(), X86_NVME_VECTOR, nvme_irq_handle, nvme_irq_quiet, &nvme_rt.IrqToken))
                return 0;
            if (!mmio_phys_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, entry_phys + 0U, 4U, 1, &addr_lo) ||
                !mmio_phys_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, entry_phys + 4U, 4U, 1, &addr_hi) ||
                !mmio_phys_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, entry_phys + 8U, 4U, 1, &data) ||
                !mmio_phys_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, entry_phys + 12U, 4U, 1, &ctrl)) {
                (void)x86_irq_registry_unbind(x86_irq_registry(), &nvme_rt.IrqToken);
                return 0;
            }
            uint32_t mc = 0U;
            if (!ecam_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, range_base, nvme_rt.Bus, nvme_rt.Device, nvme_rt.Function, cap + 2U, 2U, 0, &mc))
                return 0;
            mc = (mc | 0x8000U) & ~0x4000U;
            if (!ecam_rw_private(nvme_rt.Window, nvme_rt.UcCacheBits, range_base, nvme_rt.Bus, nvme_rt.Device, nvme_rt.Function, cap + 2U, 2U, 1, &mc))
                return 0;
            nvme_rt.TargetApicId = apic_id;
            return 1;
        }
        cap = cfg[cap + 1U] & 0xFCU;
    }
    return 0;
}

// ====== x86_nvme_start ======

int x86_nvme_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
)
{
    if (!env || !env->Window || !env->Window->Pool || !env->Context || !directory || !clock || !clock->Ready)
        return 0;

    for (uint32_t i = 0; i < sizeof(nvme_rt); ++i)
        ((uint8_t *)&nvme_rt)[i] = 0U;

    X86_FB_CPU cpu;
    if (!x86_fb_cpu(&cpu) || !x86_apic_uc_slot(cpu.Pat))
        return 0;

    uint64_t uc_cache = x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat) - 1U);

    uint64_t range_base = 0ULL;
    uint8_t cfg[256] = {0};
    int found = discover_pci_private(env, directory, uc_cache, &range_base, cfg);
    if (found < 0)
        return 0;
    if (found == 0)
        return kernel_log(1, "KERNEL_NVME_ABSENT");

    uint32_t bar0_lo = (uint32_t)cfg[0x10U] | ((uint32_t)cfg[0x11U] << 8)
        | ((uint32_t)cfg[0x12U] << 16) | ((uint32_t)cfg[0x13U] << 24);
    uint32_t bar0_hi = (uint32_t)cfg[0x14U] | ((uint32_t)cfg[0x15U] << 8)
        | ((uint32_t)cfg[0x16U] << 16) | ((uint32_t)cfg[0x17U] << 24);
    uint64_t bar0_phys = ((uint64_t)bar0_hi << 32) | (uint64_t)(bar0_lo & ~0xFULL);
    if (!bar0_phys)
        return 0;

    KERNEL_HEAP *heap = kernel_heap();
    if (!heap)
        return 0;
    void *dma_pages = kernel_heap_pages(heap, NVME_DMA_PAGES);
    if (!dma_pages)
        return 0;
    uint64_t dma_phys = 0ULL;
    if (!x86_heap_virt_to_phys((uintptr_t)dma_pages, &dma_phys))
        return 0;

    nvme_rt.Window = env->Window;
    nvme_rt.LapicMapping = env->Mapping;
    nvme_rt.Clock = *clock;
    nvme_rt.UcCacheBits = uc_cache;
    nvme_rt.Bar0Phys = bar0_phys;
    nvme_rt.Present = 1U;
    nvme_rt.Self = (uintptr_t)&nvme_rt;

    BOOT_U32 lapic_id_reg = 0U;
    uint32_t bsp_apic_id = (env->Mapping && x86_lapic_read(env->Mapping, 0x020U, &lapic_id_reg))
        ? (lapic_id_reg >> 24)
        : 0U;
    nvme_rt.IrqEnabled = enable_msix_private(range_base, cfg, bsp_apic_id) ? 1U : 0U;

    NVME_TRANSPORT transport = {
        &nvme_rt,
        transport_r32_private,
        transport_w32_private,
        transport_r64_private,
        transport_w64_private,
        transport_clock_private,
        transport_barrier_private,
        transport_wait_private,
        transport_irq_seq_private,
        clock->Mask,
        3579545U,
        nvme_rt.IrqEnabled
    };

    if (kernel_nvme_init(&nvme_rt.Driver, &transport, dma_pages, dma_phys) != KERNEL_STORAGE_OK)
        return 0;

    if (kernel_block_cache_init(&nvme_rt.Cache, heap, &nvme_rt.Driver.BlockDevice, 64U) != KERNEL_STORAGE_OK)
        return 0;

    nvme_rt.Ready = 1U;
    uint64_t bdf = ((uint64_t)nvme_rt.Bus << 8) | ((uint64_t)nvme_rt.Device << 3) | nvme_rt.Function;
    return kernel_text("KERNEL_NVME_BDF: ") && kernel_number(bdf) &&
        kernel_text("\nKERNEL_NVME_IRQ: ") && kernel_number(nvme_rt.IrqEnabled ? X86_NVME_VECTOR : 0ULL) &&
        kernel_text("\nKERNEL_NVME_SECTORS: ") && kernel_number(nvme_rt.Driver.TotalSectors) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_NVME_READY");
}

// ====== x86_nvme_stop ======

void x86_nvme_stop(void)
{
    if (nvme_rt.Self != (uintptr_t)&nvme_rt)
        return;
    if (nvme_rt.Ready) {
        (void)kernel_block_cache_flush(&nvme_rt.Cache);
        kernel_block_cache_destroy(&nvme_rt.Cache);
        kernel_nvme_shutdown(&nvme_rt.Driver);
        nvme_rt.Ready = 0U;
    }
    if (nvme_rt.IrqEnabled && nvme_rt.IrqToken.Id) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &nvme_rt.IrqToken);
        nvme_rt.IrqEnabled = 0U;
    }
}

// ====== x86_nvme_device ======

KERNEL_BLOCK_DEVICE *x86_nvme_device(void)
{
    return nvme_rt.Ready ? &nvme_rt.Driver.BlockDevice : NULL;
}

// ====== x86_nvme_cache ======

KERNEL_BLOCK_CACHE *x86_nvme_cache(void)
{
    return nvme_rt.Ready ? &nvme_rt.Cache : NULL;
}
