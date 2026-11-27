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
#include "kernel/net/net.h"
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

#define X86_VIRTIO_NET_VECTOR 0x47U

typedef struct {
    uintptr_t               Self;
    X86_PAGE_WINDOW        *Window;
    const X86_LAPIC_MAPPING *LapicMapping;
    X86_PM_CLOCK            Clock;
    uint64_t                UcCacheBits;
    uint64_t                EcamRangeBase;
    VIRTIO_NET_PCI_LAYOUT   Layout;
    uint32_t                Segment;
    uint32_t                Bus;
    uint32_t                Device;
    uint32_t                Function;
    uint32_t                Present;
    uint32_t                Ready;
    uint32_t                IrqEnabled;
    uint32_t                MsixCapOffset;
    X86_IRQ_TOKEN           IrqToken;
    uint64_t                IrqSeq;
    VIRTIO_NET_DEVICE       Driver;
} X86_VIRTIO_NET_RUNTIME;

static X86_VIRTIO_NET_RUNTIME vnet_rt;
static unsigned char vnet_mcfg_scratch[X86_MCFG_BYTES];
static X86_MCFG vnet_mcfg;

// ====== acpi_read_private ======

static int acpi_read_private(void *state, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    const ACPI_READ_CTX *ctx = (const ACPI_READ_CTX *)state;
    return x86_page_window_read_acpi(ctx->Window, ctx->Context, physical, bytes, output);
}

// ====== mmio_window_cb ======

// Intel SDM Vol. 3A §11.3: Strong Uncacheable (UC) scalar MMIO register access.
static int mmio_window_cb(void *opaque)
{
    MMIO_BORROW_REQ *req = (MMIO_BORROW_REQ *)opaque;
    volatile uint8_t *base = (volatile uint8_t *)x86_window_bytes();
    if (!base)
        return 0;

    volatile uint8_t *ptr = base + req->InPageOffset;
    _ReadWriteBarrier();
    if (req->Width == 1U) {
        if (req->Write)
            *ptr = (uint8_t)req->Value;
        else
            req->Value = *ptr;
    } else if (req->Width == 2U) {
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

static int transport_mmio_private(
    void *context,
    uint32_t region_index,
    uint32_t offset,
    uint32_t width,
    int write,
    uint32_t *value
)
{
    X86_VIRTIO_NET_RUNTIME *rt = (X86_VIRTIO_NET_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt || !value || region_index >= 4U)
        return 0;
    if (width != 1U && width != 2U && width != 4U)
        return 0;

    const VIRTIO_NET_PCI_REGION *region = &rt->Layout.Regions[region_index];
    uint64_t byte_offset = offset;
    if (region_index == VIRTIO_NET_REGION_NOTIFY)
        byte_offset = (uint64_t)offset * region->Multiplier;

    if (byte_offset > region->Length || width > region->Length - byte_offset)
        return 0;

    uint64_t phys_addr = region->Address + byte_offset;
    if (phys_addr & (width - 1ULL))
        return 0;

    uint64_t page_phys = phys_addr & ~4095ULL;
    uint32_t in_page = (uint32_t)(phys_addr & 4095ULL);
    if (in_page + width > 4096U)
        return 0;

    MMIO_BORROW_REQ req = {in_page, width, write != 0, write ? *value : 0U};
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
    const X86_VIRTIO_NET_RUNTIME *rt = (const X86_VIRTIO_NET_RUNTIME *)context;
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
    const X86_VIRTIO_NET_RUNTIME *rt = (const X86_VIRTIO_NET_RUNTIME *)context;
    return rt ? ks_atomic_load_acquire(&rt->IrqSeq) : 0ULL;
}

// ====== transport_wait_private ======

static void transport_wait_private(void *context, uint64_t expected_seq)
{
    const X86_VIRTIO_NET_RUNTIME *rt = (const X86_VIRTIO_NET_RUNTIME *)context;
    if (!rt || !rt->IrqEnabled || !ks_irq_state()) {
        _mm_pause();
        return;
    }
    uint64_t saved_if = ks_irq_save();
    if (ks_atomic_load_acquire(&rt->IrqSeq) == expected_seq) {
        _enable();
        __halt();
    } else {
        ks_irq_restore(saved_if);
    }
}

// ====== vnet_irq_handle_private ======

static int vnet_irq_handle_private(void)
{
    if (!vnet_rt.LapicMapping || !x86_lapic_eoi_vector(vnet_rt.LapicMapping, X86_VIRTIO_NET_VECTOR))
        return 0;
    ks_atomic_store_release(&vnet_rt.IrqSeq, ks_atomic_load_relaxed(&vnet_rt.IrqSeq) + 1ULL);
    return 1;
}

// ====== vnet_irq_quiet_private ======

static int vnet_irq_quiet_private(void)
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
        vnet_mcfg_scratch,
        sizeof(vnet_mcfg_scratch),
        &vnet_mcfg
    );
    if (found_mcfg == 2)
        return 0;
    if (found_mcfg != 1)
        return -1;

    int found = 0;
    for (uint32_t r = 0; r < vnet_mcfg.Count && !found; ++r) {
        const X86_ECAM_RANGE *range = &vnet_mcfg.Ranges[r];
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

                    uint16_t vendor = (uint16_t)(id & 0xFFFFU);
                    uint16_t device_id = (uint16_t)(id >> 16);
                    int match = (vendor == 0x1AF4U && device_id == 0x1041U);
                    if (!match && vendor == 0x1AF4U && device_id == 0x1000U) {
                        uint32_t sub = 0U;
                        if (ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x2CU, 4U, 0, &sub) &&
                            ((sub >> 16) == 1U))
                            match = 1;
                    }
                    if (match) {
                        // PCI Local Bus Spec 3.0 §6.2.2: Command bit 1 (Memory Space) | bit 2 (Bus Master).
                        uint32_t cmd = 0U;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 0, &cmd))
                            return -1;
                        cmd |= 0x06U;
                        if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, 0x04U, 2U, 1, &cmd))
                            return -1;

                        for (uint32_t word = 0; word < 64U; ++word) {
                            uint32_t val = 0U;
                            if (!ecam_rw_private(env->Window, uc_cache, range->Base, bus, dev, fn, word * 4U, 4U, 0, &val))
                                return -1;
                            config_out[word * 4U + 0U] = (uint8_t)(val & 0xFFU);
                            config_out[word * 4U + 1U] = (uint8_t)((val >> 8) & 0xFFU);
                            config_out[word * 4U + 2U] = (uint8_t)((val >> 16) & 0xFFU);
                            config_out[word * 4U + 3U] = (uint8_t)((val >> 24) & 0xFFU);
                        }
                        vnet_rt.EcamRangeBase = range->Base;
                        vnet_rt.Segment = range->Segment;
                        vnet_rt.Bus = bus;
                        vnet_rt.Device = dev;
                        vnet_rt.Function = fn;
                        found = 1;
                        break;
                    }

                    if (fn == 0U) {
                        uint32_t hdr = 0U;
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

// ====== enable_msix_private ======

// PCIe 4.0 §7.7.2 (MSI-X Cap 0x11) & Intel SDM Vol. 3A §11.11 (LAPIC MSI address 0xFEE00000).
static int enable_msix_private(const uint8_t pci_cfg[256], uint32_t bsp_apic_id)
{
    if (!(pci_cfg[0x06U] & 0x10U) || !vnet_rt.EcamRangeBase || !x86_irq_registry())
        return 0;
    uint32_t cap = pci_cfg[0x34U] & 0xFCU;
    uint32_t msix_cap = 0U;
    for (uint32_t steps = 0; cap >= 0x40U && cap <= 0xFCU && steps < 48U; ++steps) {
        if (pci_cfg[cap] == 0x11U) {
            msix_cap = cap;
            break;
        }
        cap = pci_cfg[cap + 1U] & 0xFCU;
    }
    if (!msix_cap || msix_cap + 8U > 256U)
        return 0;

    uint32_t table_info = (uint32_t)pci_cfg[msix_cap + 4U]
        | ((uint32_t)pci_cfg[msix_cap + 5U] << 8)
        | ((uint32_t)pci_cfg[msix_cap + 6U] << 16)
        | ((uint32_t)pci_cfg[msix_cap + 7U] << 24);
    uint32_t bir = table_info & 0x7U;
    uint32_t table_off = table_info & ~0x7U;
    if (bir >= 6U)
        return 0;

    uint32_t bar_off = 0x10U + bir * 4U;
    uint32_t bar_lo = (uint32_t)pci_cfg[bar_off]
        | ((uint32_t)pci_cfg[bar_off + 1U] << 8)
        | ((uint32_t)pci_cfg[bar_off + 2U] << 16)
        | ((uint32_t)pci_cfg[bar_off + 3U] << 24);
    if ((bar_lo & 1U) != 0U)
        return 0;
    uint64_t bar_phys = (uint64_t)(bar_lo & ~0xFU);
    if ((bar_lo & 0x6U) == 0x4U && bir + 1U < 6U) {
        uint32_t bar_hi = (uint32_t)pci_cfg[bar_off + 4U]
            | ((uint32_t)pci_cfg[bar_off + 5U] << 8)
            | ((uint32_t)pci_cfg[bar_off + 6U] << 16)
            | ((uint32_t)pci_cfg[bar_off + 7U] << 24);
        bar_phys |= ((uint64_t)bar_hi << 32);
    }
    if (!bar_phys)
        return 0;

    if (!x86_irq_registry_bind(x86_irq_registry(), X86_VIRTIO_NET_VECTOR,
            vnet_irq_handle_private, vnet_irq_quiet_private, &vnet_rt.IrqToken))
        return 0;

    uint64_t entry_phys = bar_phys + table_off;
    uint32_t entry_words[4] = {
        0xFEE00000U | ((bsp_apic_id & 0xFFU) << 12),
        0U,
        X86_VIRTIO_NET_VECTOR,
        0U
    };
    for (uint32_t w = 0; w < 4U; ++w) {
        uint64_t addr = entry_phys + (uint64_t)w * 4ULL;
        MMIO_BORROW_REQ req = {(uint32_t)(addr & 4095ULL), 4U, 1, entry_words[w]};
        uint64_t saved_if = ks_irq_save();
        int ok = x86_apic_borrow(vnet_rt.Window, addr & ~4095ULL, vnet_rt.UcCacheBits, 1, mmio_window_cb, &req);
        ks_irq_restore(saved_if);
        if (!ok) {
            (void)x86_irq_registry_unbind(x86_irq_registry(), &vnet_rt.IrqToken);
            return 0;
        }
    }

    uint32_t ctrl = 0U, readback = 0U, cmd = 0U;
    if (!ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
            vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, msix_cap + 2U, 2U, 0, &ctrl)) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &vnet_rt.IrqToken);
        return 0;
    }
    ctrl = (ctrl & ~0x4000U) | 0x8000U;
    if (!ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
            vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, msix_cap + 2U, 2U, 1, &ctrl) ||
        !ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
            vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, msix_cap + 2U, 2U, 0, &readback) ||
        (readback & 0xC000U) != 0x8000U) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &vnet_rt.IrqToken);
        return 0;
    }
    if (ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
            vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, 0x04U, 2U, 0, &cmd)) {
        cmd |= 0x0400U;
        (void)ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
            vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, 0x04U, 2U, 1, &cmd);
    }
    vnet_rt.MsixCapOffset = msix_cap;
    return 1;
}

// ====== x86_virtio_net_stop ======

void x86_virtio_net_stop(void)
{
    if (!vnet_rt.IrqEnabled)
        return;
    if (vnet_rt.MsixCapOffset && vnet_rt.EcamRangeBase) {
        uint32_t ctrl = 0U;
        if (ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
                vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, vnet_rt.MsixCapOffset + 2U, 2U, 0, &ctrl)) {
            ctrl = (ctrl & ~0x8000U) | 0x4000U;
            (void)ecam_rw_private(vnet_rt.Window, vnet_rt.UcCacheBits, vnet_rt.EcamRangeBase,
                vnet_rt.Bus, vnet_rt.Device, vnet_rt.Function, vnet_rt.MsixCapOffset + 2U, 2U, 1, &ctrl);
        }
    }
    if (x86_irq_registry())
        (void)x86_irq_registry_unbind(x86_irq_registry(), &vnet_rt.IrqToken);
    vnet_rt.IrqEnabled = 0U;
}

// ====== x86_virtio_net_start ======

int x86_virtio_net_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
)
{
    if (vnet_rt.Ready)
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

    for (uint32_t i = 0; i < sizeof(vnet_rt); ++i)
        ((uint8_t *)&vnet_rt)[i] = 0U;

    uint64_t uc_cache = x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat) - 1U);
    uint8_t pci_cfg[256];
    int found = discover_pci_private(env, directory, uc_cache, pci_cfg);
    if (found < 0)
        return 0;
    if (found == 0)
        return kernel_log(1, "KERNEL_VIRTIO_NET_ABSENT");

    uint32_t probe = kernel_virtio_net_pci_probe(
        pci_cfg,
        256U,
        env->Window->Pool->PhysicalBits,
        &vnet_rt.Layout
    );
    if (probe != KVNET_PCI_READY)
        return 0;

    uint8_t *dma_pages = (uint8_t *)kernel_heap_pages(heap, VIRTIO_NET_DMA_PAGES);
    if (!dma_pages)
        return 0;

    uintptr_t dma_virt = (uintptr_t)dma_pages;
    uint64_t dma_bytes = (uint64_t)VIRTIO_NET_DMA_PAGES * 4096ULL;
    if (dma_virt < X86_HEAP_VIRTUAL ||
        dma_virt + dma_bytes > X86_HEAP_VIRTUAL + (uint64_t)X86_HEAP_PAGES * 4096ULL)
        return 0;

    uint64_t dma_phys = heap_auth->Data.Base + (dma_virt - X86_HEAP_VIRTUAL);

    vnet_rt.Window = env->Window;
    vnet_rt.LapicMapping = env->Mapping;
    vnet_rt.Clock = *clock;
    vnet_rt.UcCacheBits = uc_cache;
    vnet_rt.Present = 1U;
    vnet_rt.Self = (uintptr_t)&vnet_rt;

    BOOT_U32 lapic_id_reg = 0U;
    uint32_t bsp_apic_id = (env->Mapping && x86_lapic_read(env->Mapping, 0x020U, &lapic_id_reg))
        ? (lapic_id_reg >> 24)
        : 0U;
    vnet_rt.IrqEnabled = enable_msix_private(pci_cfg, bsp_apic_id) ? 1U : 0U;

    VIRTIO_NET_TRANSPORT transport = {
        &vnet_rt,
        transport_mmio_private,
        transport_clock_private,
        transport_barrier_private,
        clock->Mask,
        3579545U,
        1000000U,
        transport_wait_private,
        transport_irq_seq_private,
        vnet_rt.IrqEnabled
    };

    if (kernel_virtio_net_init(&vnet_rt.Driver, &transport, dma_pages, dma_phys) != KERNEL_NET_OK)
        return 0;

    vnet_rt.Ready = 1U;
    uint64_t bdf = ((uint64_t)vnet_rt.Bus << 8) | ((uint64_t)vnet_rt.Device << 3) | vnet_rt.Function;
    uint64_t mac_packed = ((uint64_t)vnet_rt.Driver.NetDevice.Mac[0] << 40)
        | ((uint64_t)vnet_rt.Driver.NetDevice.Mac[1] << 32)
        | ((uint64_t)vnet_rt.Driver.NetDevice.Mac[2] << 24)
        | ((uint64_t)vnet_rt.Driver.NetDevice.Mac[3] << 16)
        | ((uint64_t)vnet_rt.Driver.NetDevice.Mac[4] << 8)
        | (uint64_t)vnet_rt.Driver.NetDevice.Mac[5];

    if (!(kernel_text("KERNEL_VIRTIO_NET_BDF: ") && kernel_number(bdf) &&
        kernel_text("\nKERNEL_VIRTIO_NET_IRQ: ") && kernel_number(vnet_rt.IrqEnabled ? X86_VIRTIO_NET_VECTOR : 0ULL) &&
        kernel_text("\nKERNEL_VIRTIO_NET_MAC: ") && kernel_number(mac_packed) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_VIRTIO_NET_READY")))
        return 0;

    KERNEL_CLOCK net_clock = {transport_clock_private, &vnet_rt, clock->Mask, KERNEL_PM_TIMER_HZ};
    (void)kernel_net_start(heap, &vnet_rt.Driver.NetDevice, &net_clock);
    KERNEL_NET_INFO ninfo;
    if (kernel_net_info(&ninfo) && ninfo.Configured && ninfo.Gateway) {
        uint32_t rtt_ms = 0U;
        if (kernel_net_ping(ninfo.Gateway, 1500U, &rtt_ms) == KERNEL_NET_OK) {
            (void)(kernel_text("KERNEL_NET_PING_GW_MS: ") && kernel_number(rtt_ms) &&
                kernel_text("\n") && kernel_log(0, "KERNEL_NET_PING_GW_OK"));
        }
    }
    return 1;
}

// ====== x86_virtio_net_device ======

KERNEL_NET_DEVICE *x86_virtio_net_device(void)
{
    return vnet_rt.Ready ? &vnet_rt.Driver.NetDevice : NULL;
}
