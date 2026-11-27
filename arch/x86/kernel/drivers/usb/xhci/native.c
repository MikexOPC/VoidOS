#include "native.h"
#include <intrin.h>
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/mm/paging/heap/authority.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "kernel/diagnostics/log.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/sync/internal.h"
#include "kernel/vfs/vfs.h"
#ifdef KERNEL_USER_PROCESS
#include "kernel/gui/gui.h"
#include "kernel/console/video/pointer/state.h"
#include "arch/x86/kernel/process/gui.h"
#endif

typedef struct {
    X86_PAGE_WINDOW           *Window;
    const KERNEL_BOOT_CONTEXT *Context;
} ACPI_READ_CTX;

typedef struct {
    uint32_t  InPageOffset;
    uint32_t  Width;
    int       Write;
    uint32_t  Value;
} MMIO_BORROW_REQ;

#define X86_XHCI_VECTOR         0x46U
#define X86_XHCI_MMIO_LIMIT     0x10000U
#define X86_XHCI_SHELL_Q_CAP    64U

typedef struct {
    uintptr_t                 Self;
    X86_PAGE_WINDOW          *Window;
    const X86_LAPIC_MAPPING  *LapicMapping;
    const X86_HEAP_AUTH      *HeapAuth;
    KERNEL_HEAP              *Heap;
    X86_PM_CLOCK              Clock;
    uint64_t                  UcCacheBits;
    uint64_t                  EcamRangeBase;
    uint64_t                  Bar0Phys;
    uint32_t                  Segment;
    uint32_t                  Bus;
    uint32_t                  Device;
    uint32_t                  Function;
    uint32_t                  Present;
    uint32_t                  Ready;
    uint32_t                  IrqEnabled;
    uint32_t                  MsixCapOffset;
    uint32_t                  MsiCapOffset;
    X86_IRQ_TOKEN             IrqToken;
    uint64_t                  IrqSeq;
    volatile long             PollBusy;
    int32_t                   ScaleRemainderX;
    int32_t                   ScaleRemainderY;
    uint64_t                  KeyEvents;
    uint64_t                  MouseEvents;
    uint32_t                  LastConnects;
    uint32_t                  LastDisconnects;
    uint32_t                  ShellScalars[X86_XHCI_SHELL_Q_CAP];
    uint32_t                  ShellHead;
    uint32_t                  ShellTail;
    uint32_t                  MscReady;
    KERNEL_BLOCK_DEVICE       MscDevice;
    KERNEL_BLOCK_CACHE        MscCache;
    KERNEL_XHCI_DEVICE        Driver;
} X86_XHCI_RUNTIME;

static X86_XHCI_RUNTIME xhci_rt;
static unsigned char xhci_mcfg_scratch[X86_MCFG_BYTES];
static X86_MCFG xhci_mcfg;

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

// ====== mmio_rw32_private ======

static int mmio_rw32_private(X86_XHCI_RUNTIME *rt, uint32_t offset, int write, uint32_t *value)
{
    if (!rt || rt->Self != (uintptr_t)rt || !value || (offset & 3U) != 0U || offset + 4U > X86_XHCI_MMIO_LIMIT)
        return 0;

    uint64_t phys_addr = rt->Bar0Phys + offset;
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

// ====== transport_read32_private ======

static int transport_read32_private(void *context, uint32_t offset, uint32_t *val)
{
    return mmio_rw32_private((X86_XHCI_RUNTIME *)context, offset, 0, val);
}

// ====== transport_write32_private ======

static int transport_write32_private(void *context, uint32_t offset, uint32_t val)
{
    return mmio_rw32_private((X86_XHCI_RUNTIME *)context, offset, 1, &val);
}

// ====== transport_write64_private ======

static int transport_write64_private(void *context, uint32_t offset, uint64_t val)
{
    // xHCI Spec Rev. 1.2 §5.5: 64-bit registers may be written as two 32-bit writes (low dword first).
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFULL);
    uint32_t hi = (uint32_t)(val >> 32);
    return mmio_rw32_private((X86_XHCI_RUNTIME *)context, offset, 1, &lo) &&
           mmio_rw32_private((X86_XHCI_RUNTIME *)context, offset + 4U, 1, &hi);
}

// ====== transport_barrier_private ======

static void transport_barrier_private(void *context)
{
    (void)context;
    _ReadWriteBarrier();
    _mm_mfence();
    _ReadWriteBarrier();
}

// ====== transport_alloc_dma_page_private ======

static void *transport_alloc_dma_page_private(void *context, uint64_t *phys_out)
{
    X86_XHCI_RUNTIME *rt = (X86_XHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->Heap || !rt->HeapAuth || !phys_out)
        return NULL;

    uint8_t *page = (uint8_t *)kernel_heap_pages(rt->Heap, 1U);
    if (!page)
        return NULL;

    uintptr_t virt = (uintptr_t)page;
    if (virt < X86_HEAP_VIRTUAL ||
        virt + 4096ULL > X86_HEAP_VIRTUAL + (uint64_t)X86_HEAP_PAGES * 4096ULL)
        return NULL;

    for (uint32_t i = 0; i < 4096U; ++i)
        page[i] = 0U;

    *phys_out = rt->HeapAuth->Data.Base + (virt - X86_HEAP_VIRTUAL);
    return page;
}

// ====== transport_clock_private ======

static int transport_clock_private(void *context, uint32_t *ticks_out)
{
    const X86_XHCI_RUNTIME *rt = (const X86_XHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt || !ticks_out || !rt->Clock.Ready)
        return 0;
    *ticks_out = x86_pm_read((unsigned short)rt->Clock.Port) & rt->Clock.Mask;
    return 1;
}

// ====== push_shell_scalar_private ======

static void push_shell_scalar_private(X86_XHCI_RUNTIME *rt, uint32_t scalar)
{
    uint64_t saved_if = ks_irq_save();
    uint32_t next_tail = (rt->ShellTail + 1U) % X86_XHCI_SHELL_Q_CAP;
    if (next_tail != rt->ShellHead) {
        rt->ShellScalars[rt->ShellTail] = scalar;
        rt->ShellTail = next_tail;
    }
    ks_irq_restore(saved_if);
}

// ====== on_usb_key_private ======

static void on_usb_key_private(void *context, uint32_t code, uint32_t scalar, uint32_t flags)
{
    X86_XHCI_RUNTIME *rt = (X86_XHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt)
        return;
    ++rt->KeyEvents;
#ifdef KERNEL_USER_PROCESS
    KERNEL_GUI *gui = kernel_gui_global();
    if (gui && kernel_gui_display_owner(gui) != 0) {
        (void)kernel_gui_input_push_key(gui, code, scalar, flags);
        return;
    }
#else
    (void)flags;
#endif
    if ((code & XHCI_KEY_EVENT_RELEASE) == 0U) {
        if (scalar != 0U)
            push_shell_scalar_private(rt, scalar);
        else if ((code & 0xFFFFU) == 0x29U)
            push_shell_scalar_private(rt, 0x1BU);
    }
}

// ====== on_usb_mouse_private ======

static void on_usb_mouse_private(void *context, int32_t dx, int32_t dy, uint32_t buttons)
{
    X86_XHCI_RUNTIME *rt = (X86_XHCI_RUNTIME *)context;
    if (!rt || rt->Self != (uintptr_t)rt)
        return;
    ++rt->MouseEvents;
#ifdef KERNEL_USER_PROCESS
    KERNEL_GUI *gui = kernel_gui_global();
    if (gui && kernel_gui_display_owner(gui) != 0) {
        int32_t screen_dx = kernel_pointer_scale_delta(dx, &rt->ScaleRemainderX);
        int32_t screen_dy = kernel_pointer_scale_delta(dy, &rt->ScaleRemainderY);
        int32_t wheel = (int32_t)(int8_t)((buttons >> 8) & 0xFFU);
        (void)kernel_gui_input_push_mouse(gui, screen_dx, screen_dy, buttons & 7U);
        if (wheel != 0)
            (void)kernel_gui_input_push_scroll(gui, wheel);
        int32_t x = 0, y = 0;
        if (kernel_gui_pointer_position(gui, &x, &y))
            x86_gui_cursor_position(x, y);
    }
#else
    (void)dx;
    (void)dy;
    (void)buttons;
#endif
}

// ====== xhci_irq_handle_private ======

static int xhci_irq_handle_private(void)
{
    if (!xhci_rt.LapicMapping || !x86_lapic_eoi_vector(xhci_rt.LapicMapping, X86_XHCI_VECTOR))
        return 0;
    ks_atomic_store_release(&xhci_rt.IrqSeq, ks_atomic_load_relaxed(&xhci_rt.IrqSeq) + 1ULL);
    X86_PS2_SESSION *owner = x86_ps2_session();
    if (owner && owner->Active && !owner->Quiet && owner->XhciBinding) {
        owner->Ready = 1;
        (void)kernel_scheduler_irq_post_bound(3U, (BOOT_U64)&owner->XhciBinding, owner->XhciBinding);
    }
    return 1;
}

// ====== xhci_irq_quiet_private ======

static int xhci_irq_quiet_private(void)
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
        xhci_mcfg_scratch,
        sizeof(xhci_mcfg_scratch),
        &xhci_mcfg
    );
    if (found_mcfg == 2)
        return 0;
    if (found_mcfg != 1)
        return -1;

    int found = 0;
    for (uint32_t r = 0; r < xhci_mcfg.Count && !found; ++r) {
        const X86_ECAM_RANGE *range = &xhci_mcfg.Ranges[r];
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

                    // xHCI Spec Rev. 1.2 §5.2: Class 0x0C (Serial Bus), Subclass 0x03 (USB), ProgIF 0x30 (xHCI).
                    if ((class_rev >> 8) == 0x0C0330U) {
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
                        xhci_rt.EcamRangeBase = range->Base;
                        xhci_rt.Segment = range->Segment;
                        xhci_rt.Bus = bus;
                        xhci_rt.Device = dev;
                        xhci_rt.Function = fn;
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

// ====== parse_bar0_private ======

static int parse_bar0_private(const uint8_t pci_cfg[256], uint32_t phys_bits, uint64_t *bar_out)
{
    uint32_t bar0 = (uint32_t)pci_cfg[0x10U] |
                    ((uint32_t)pci_cfg[0x11U] << 8) |
                    ((uint32_t)pci_cfg[0x12U] << 16) |
                    ((uint32_t)pci_cfg[0x13U] << 24);
    if ((bar0 & 1U) != 0U)
        return 0;

    uint64_t addr = (uint64_t)(bar0 & ~0xFU);
    uint32_t type = (bar0 >> 1) & 0x3U;
    if (type == 0x2U) {
        uint32_t bar1 = (uint32_t)pci_cfg[0x14U] |
                        ((uint32_t)pci_cfg[0x15U] << 8) |
                        ((uint32_t)pci_cfg[0x16U] << 16) |
                        ((uint32_t)pci_cfg[0x17U] << 24);
        addr |= ((uint64_t)bar1 << 32);
    } else if (type != 0x0U) {
        return 0;
    }

    if (!addr || (addr & 4095ULL) != 0ULL)
        return 0;
    if (phys_bits < 64U && addr >= (1ULL << phys_bits))
        return 0;

    *bar_out = addr;
    return 1;
}

// ====== enable_msix_private ======

// PCIe 4.0 §7.7.2 (MSI-X Cap 0x11) & Intel SDM Vol. 3A §11.11 (LAPIC MSI address 0xFEE00000).
static int enable_msix_private(const uint8_t pci_cfg[256], uint32_t bsp_apic_id)
{
    if (!(pci_cfg[0x06U] & 0x10U) || !xhci_rt.EcamRangeBase || !x86_irq_registry())
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

    if (!x86_irq_registry_bind(x86_irq_registry(), X86_XHCI_VECTOR,
            xhci_irq_handle_private, xhci_irq_quiet_private, &xhci_rt.IrqToken))
        return 0;

    uint64_t entry_phys = bar_phys + table_off;
    uint32_t entry_words[4] = {
        0xFEE00000U | ((bsp_apic_id & 0xFFU) << 12),
        0U,
        X86_XHCI_VECTOR,
        0U
    };
    for (uint32_t w = 0; w < 4U; ++w) {
        uint64_t addr = entry_phys + (uint64_t)w * 4ULL;
        MMIO_BORROW_REQ req = {(uint32_t)(addr & 4095ULL), 4U, 1, entry_words[w]};
        uint64_t saved_if = ks_irq_save();
        int ok = x86_apic_borrow(xhci_rt.Window, addr & ~4095ULL, xhci_rt.UcCacheBits, 1, mmio_window_cb, &req);
        ks_irq_restore(saved_if);
        if (!ok) {
            (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
            return 0;
        }
    }

    uint32_t ctrl = 0, readback = 0, cmd = 0;
    if (!ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msix_cap + 2U, 2U, 0, &ctrl)) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        return 0;
    }
    ctrl = (ctrl & ~0x4000U) | 0x8000U;
    if (!ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msix_cap + 2U, 2U, 1, &ctrl) ||
        !ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msix_cap + 2U, 2U, 0, &readback) ||
        (readback & 0xC000U) != 0x8000U) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        return 0;
    }
    if (ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, 0x04U, 2U, 0, &cmd)) {
        cmd |= 0x0400U;
        (void)ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, 0x04U, 2U, 1, &cmd);
    }
    xhci_rt.MsixCapOffset = msix_cap;
    return 1;
}

// ====== enable_msi_private ======

// PCI Local Bus Spec 3.0 §6.8.1 (MSI Cap 0x05) & Intel SDM Vol. 3A §11.11 (LAPIC MSI address 0xFEE00000).
static int enable_msi_private(const uint8_t pci_cfg[256], uint32_t bsp_apic_id)
{
    if (!(pci_cfg[0x06U] & 0x10U) || !xhci_rt.EcamRangeBase || !x86_irq_registry())
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

    if (!x86_irq_registry_bind(x86_irq_registry(), X86_XHCI_VECTOR,
            xhci_irq_handle_private, xhci_irq_quiet_private, &xhci_rt.IrqToken))
        return 0;

    uint32_t ctrl = 0, readback = 0, cmd = 0;
    if (!ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 2U, 2U, 0, &ctrl)) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        return 0;
    }
    int is_64 = (ctrl & 0x0080U) != 0U;
    uint32_t addr_lo = 0xFEE00000U | ((bsp_apic_id & 0xFFU) << 12);
    uint32_t addr_hi = 0U;
    uint32_t data = X86_XHCI_VECTOR;
    int ok = ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
        xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 4U, 4U, 1, &addr_lo);
    if (ok && is_64) {
        ok = ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 8U, 4U, 1, &addr_hi) &&
            ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 12U, 2U, 1, &data);
    } else if (ok) {
        ok = ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 8U, 2U, 1, &data);
    }
    if (!ok) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        return 0;
    }

    ctrl = (ctrl & ~0x0070U) | 0x0001U;
    if (!ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 2U, 2U, 1, &ctrl) ||
        !ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, msi_cap + 2U, 2U, 0, &readback) ||
        (readback & 0x0001U) == 0U) {
        (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        return 0;
    }
    if (ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, 0x04U, 2U, 0, &cmd)) {
        cmd |= 0x0400U;
        (void)ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
            xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, 0x04U, 2U, 1, &cmd);
    }
    xhci_rt.MsiCapOffset = msi_cap;
    return 1;
}

// ====== msc_block_read_private ======

static int msc_block_read_private(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, void *buffer)
{
    X86_XHCI_RUNTIME *rt = device ? (X86_XHCI_RUNTIME *)device->Context : NULL;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->Ready || !rt->MscReady)
        return KERNEL_STORAGE_ERR_IO;
    if (!buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;
    if (lba >= device->TotalSectors || sector_count > device->TotalSectors - lba)
        return KERNEL_STORAGE_ERR_RANGE;

    while (_InterlockedCompareExchange(&rt->PollBusy, 1L, 0L) != 0L)
        _mm_pause();
    int ok = kernel_xhci_msc_read_sectors(&rt->Driver, lba, sector_count, buffer);
    _InterlockedExchange(&rt->PollBusy, 0L);
    return ok ? KERNEL_STORAGE_OK : KERNEL_STORAGE_ERR_IO;
}

// ====== msc_block_write_private ======

static int msc_block_write_private(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, const void *buffer)
{
    X86_XHCI_RUNTIME *rt = device ? (X86_XHCI_RUNTIME *)device->Context : NULL;
    if (!rt || rt->Self != (uintptr_t)rt || !rt->Ready || !rt->MscReady)
        return KERNEL_STORAGE_ERR_IO;
    if (!buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;
    if (lba >= device->TotalSectors || sector_count > device->TotalSectors - lba)
        return KERNEL_STORAGE_ERR_RANGE;

    while (_InterlockedCompareExchange(&rt->PollBusy, 1L, 0L) != 0L)
        _mm_pause();
    int ok = kernel_xhci_msc_write_sectors(&rt->Driver, lba, sector_count, buffer);
    _InterlockedExchange(&rt->PollBusy, 0L);
    return ok ? KERNEL_STORAGE_OK : KERNEL_STORAGE_ERR_IO;
}

// ====== msc_block_flush_private ======

static int msc_block_flush_private(KERNEL_BLOCK_DEVICE *device)
{
    X86_XHCI_RUNTIME *rt = device ? (X86_XHCI_RUNTIME *)device->Context : NULL;
    return (rt && rt->Self == (uintptr_t)rt && rt->Ready && rt->MscReady)
        ? KERNEL_STORAGE_OK
        : KERNEL_STORAGE_ERR_IO;
}

// ====== x86_xhci_start ======

int x86_xhci_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
)
{
    if (xhci_rt.Ready)
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

    for (uint32_t i = 0; i < sizeof(xhci_rt); ++i)
        ((uint8_t *)&xhci_rt)[i] = 0;

    uint64_t uc_cache = x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat) - 1U);
    uint8_t pci_cfg[256];
    int found = discover_pci_private(env, directory, uc_cache, pci_cfg);
    if (found < 0)
        return 0;
    if (found == 0)
        return kernel_log(1, "KERNEL_XHCI_ABSENT");

    if (!parse_bar0_private(pci_cfg, env->Window->Pool->PhysicalBits, &xhci_rt.Bar0Phys))
        return 0;

    xhci_rt.Window = env->Window;
    xhci_rt.LapicMapping = env->Mapping;
    xhci_rt.HeapAuth = heap_auth;
    xhci_rt.Heap = heap;
    xhci_rt.Clock = *clock;
    xhci_rt.UcCacheBits = uc_cache;
    xhci_rt.Present = 1U;
    xhci_rt.Self = (uintptr_t)&xhci_rt;

    BOOT_U32 lapic_id_reg = 0;
    uint32_t bsp_apic_id = 0U;
    if (env->Mapping && x86_lapic_read(env->Mapping, 0x020U, &lapic_id_reg)) {
        bsp_apic_id = x86_lapic_x2apic_active() ? (lapic_id_reg & 0xFFU) : (lapic_id_reg >> 24);
    }
    xhci_rt.IrqEnabled = (enable_msix_private(pci_cfg, bsp_apic_id) ||
                          enable_msi_private(pci_cfg, bsp_apic_id)) ? 1U : 0U;

    KERNEL_XHCI_TRANSPORT transport = {
        &xhci_rt,
        3579545U,
        clock->Mask,
        transport_read32_private,
        transport_write32_private,
        transport_write64_private,
        transport_barrier_private,
        transport_alloc_dma_page_private,
        transport_clock_private,
        on_usb_key_private,
        on_usb_mouse_private
    };

    if (!kernel_xhci_init(&xhci_rt.Driver, &transport)) {
        x86_xhci_stop();
        return kernel_log(2, "KERNEL_XHCI_INIT_FAILED");
    }

    xhci_rt.Ready = 1U;
    uint64_t bdf = ((uint64_t)xhci_rt.Bus << 8) | ((uint64_t)xhci_rt.Device << 3) | xhci_rt.Function;
    if (!kernel_text("KERNEL_XHCI_BDF: ") || !kernel_number(bdf) ||
        !kernel_text("\nKERNEL_XHCI_IRQ: ") || !kernel_number(xhci_rt.IrqEnabled ? X86_XHCI_VECTOR : 0ULL) ||
        !kernel_text("\nKERNEL_XHCI_PORTS: ") || !kernel_number(xhci_rt.Driver.MaxPorts) ||
        !kernel_text("\nKERNEL_XHCI_HID_DEVICES: ") || !kernel_number(xhci_rt.Driver.HidCount) ||
        !kernel_text("\n"))
        return 0;

    for (uint32_t i = 0; i < xhci_rt.Driver.HidCount; ++i) {
        const XHCI_HID_SLOT *hid = &xhci_rt.Driver.HidDevices[i];
        if (!hid->Active)
            continue;
        if (hid->Protocol == USB_PROTO_KEYBOARD) {
            if (!kernel_log(0, "KERNEL_XHCI_HID_KEYBOARD"))
                return 0;
        } else if (hid->Protocol == USB_PROTO_MOUSE) {
            if (!kernel_log(0, "KERNEL_XHCI_HID_MOUSE"))
                return 0;
        }
    }

    if (xhci_rt.Driver.Msc.Active && xhci_rt.Driver.Msc.TotalSectors > 0ULL) {
        xhci_rt.MscDevice.Self = (uintptr_t)&xhci_rt.MscDevice;
        xhci_rt.MscDevice.Name = "usb-msc0";
        xhci_rt.MscDevice.Context = &xhci_rt;
        xhci_rt.MscDevice.TotalSectors = xhci_rt.Driver.Msc.TotalSectors;
        xhci_rt.MscDevice.SectorBytes = 512U;
        xhci_rt.MscDevice.MaxTransferSectors = 8U;
        xhci_rt.MscDevice.Kind = KERNEL_BLOCK_KIND_USB_MSC;
        xhci_rt.MscDevice.ReadOnly = 0U;
        xhci_rt.MscDevice.Ready = 1U;
        xhci_rt.MscDevice.Read = msc_block_read_private;
        xhci_rt.MscDevice.Write = msc_block_write_private;
        xhci_rt.MscDevice.Flush = msc_block_flush_private;
        if (kernel_block_cache_init(&xhci_rt.MscCache, heap, &xhci_rt.MscDevice, 64U) == KERNEL_STORAGE_OK) {
            xhci_rt.MscReady = 1U;
            if (!kernel_text("KERNEL_XHCI_MSC_SECTORS: ") ||
                !kernel_number(xhci_rt.MscDevice.TotalSectors) ||
                !kernel_text("\n") ||
                !kernel_log(0, "KERNEL_XHCI_MSC_READY"))
                return 0;
        }
    }

    return kernel_log(0, "KERNEL_XHCI_READY");
}

// ====== x86_xhci_poll ======

uint32_t x86_xhci_poll(void)
{
    if (xhci_rt.Self != (uintptr_t)&xhci_rt || !xhci_rt.Ready)
        return 0U;
    if (_InterlockedCompareExchange(&xhci_rt.PollBusy, 1L, 0L) != 0L)
        return 0U;
    uint32_t events = kernel_xhci_poll_events(&xhci_rt.Driver);
    int msc_unmount = 0;
    int msc_remount = 0;
    if (xhci_rt.Driver.HotplugDisconnects != xhci_rt.LastDisconnects) {
        xhci_rt.LastDisconnects = xhci_rt.Driver.HotplugDisconnects;
        (void)kernel_text("KERNEL_XHCI_HOTPLUG_DISCONNECT: ");
        (void)kernel_number(xhci_rt.Driver.HidCount);
        (void)kernel_text("\n");
    }
    if (xhci_rt.Driver.HotplugConnects != xhci_rt.LastConnects) {
        xhci_rt.LastConnects = xhci_rt.Driver.HotplugConnects;
        (void)kernel_text("KERNEL_XHCI_HOTPLUG_CONNECT: ");
        (void)kernel_number(xhci_rt.Driver.HidCount);
        (void)kernel_text("\n");
    }
    if (xhci_rt.MscReady && !xhci_rt.Driver.Msc.Active) {
        xhci_rt.MscReady = 0U;
        xhci_rt.MscDevice.Ready = 0U;
        msc_unmount = 1;
    } else if (!xhci_rt.MscReady && xhci_rt.Driver.Msc.Active && xhci_rt.Driver.Msc.TotalSectors > 0ULL) {
        xhci_rt.MscDevice.Self = (uintptr_t)&xhci_rt.MscDevice;
        xhci_rt.MscDevice.Name = "usb-msc0";
        xhci_rt.MscDevice.Context = &xhci_rt;
        xhci_rt.MscDevice.TotalSectors = xhci_rt.Driver.Msc.TotalSectors;
        xhci_rt.MscDevice.SectorBytes = 512U;
        xhci_rt.MscDevice.MaxTransferSectors = 8U;
        xhci_rt.MscDevice.Kind = KERNEL_BLOCK_KIND_USB_MSC;
        xhci_rt.MscDevice.ReadOnly = 0U;
        xhci_rt.MscDevice.Ready = 1U;
        xhci_rt.MscDevice.Read = msc_block_read_private;
        xhci_rt.MscDevice.Write = msc_block_write_private;
        xhci_rt.MscDevice.Flush = msc_block_flush_private;
        if (!xhci_rt.MscCache.Ready) {
            if (kernel_block_cache_init(&xhci_rt.MscCache, xhci_rt.Heap, &xhci_rt.MscDevice, 64U) == KERNEL_STORAGE_OK)
                xhci_rt.MscReady = 1U;
        } else {
            (void)kernel_block_cache_invalidate(&xhci_rt.MscCache);
            xhci_rt.MscReady = 1U;
        }
        msc_remount = xhci_rt.MscReady != 0U;
    }
    _InterlockedExchange(&xhci_rt.PollBusy, 0L);

    if (msc_unmount) {
        if (xhci_rt.MscCache.Ready)
            (void)kernel_block_cache_invalidate(&xhci_rt.MscCache);
        KERNEL_VFS *vfs = kernel_vfs();
        if (vfs && kernel_vfs_unmount(vfs, "/usb") == KERNEL_STORAGE_OK)
            (void)kernel_log(1, "KERNEL_VFS_USB_UNMOUNTED");
    } else if (msc_remount) {
        (void)kernel_text("KERNEL_XHCI_MSC_SECTORS: ");
        (void)kernel_number(xhci_rt.MscDevice.TotalSectors);
        (void)kernel_text("\n");
        (void)kernel_log(0, "KERNEL_XHCI_MSC_READY");
        KERNEL_VFS *vfs = kernel_vfs();
        if (vfs && kernel_vfs_mount_fat(vfs, "/usb", &xhci_rt.MscCache) == KERNEL_STORAGE_OK)
            (void)kernel_log(0, "KERNEL_VFS_USB_MOUNTED");
    }
    return events;
}

// ====== x86_xhci_has_shell_scalar ======

int x86_xhci_has_shell_scalar(void)
{
    if (xhci_rt.Self != (uintptr_t)&xhci_rt || !xhci_rt.Ready)
        return 0;
    uint64_t saved_if = ks_irq_save();
    int pending = (xhci_rt.ShellHead != xhci_rt.ShellTail);
    ks_irq_restore(saved_if);
    return pending;
}

// ====== x86_xhci_pop_shell_scalar ======

int x86_xhci_pop_shell_scalar(uint32_t *scalar_out)
{
    if (!scalar_out || xhci_rt.Self != (uintptr_t)&xhci_rt || !xhci_rt.Ready)
        return 0;
    int got = 0;
    uint64_t saved_if = ks_irq_save();
    if (xhci_rt.ShellHead != xhci_rt.ShellTail) {
        *scalar_out = xhci_rt.ShellScalars[xhci_rt.ShellHead];
        xhci_rt.ShellHead = (xhci_rt.ShellHead + 1U) % X86_XHCI_SHELL_Q_CAP;
        got = 1;
    }
    ks_irq_restore(saved_if);
    return got;
}

// ====== x86_xhci_stop ======

void x86_xhci_stop(void)
{
    if (xhci_rt.Self == (uintptr_t)&xhci_rt && xhci_rt.Ready) {
        (void)kernel_text("KERNEL_XHCI_STATS_IRQS: ");
        (void)kernel_number(ks_atomic_load_acquire(&xhci_rt.IrqSeq));
        (void)kernel_text("\nKERNEL_XHCI_STATS_KEYS: ");
        (void)kernel_number(xhci_rt.KeyEvents);
        (void)kernel_text("\nKERNEL_XHCI_STATS_MOUSE: ");
        (void)kernel_number(xhci_rt.MouseEvents);
        (void)kernel_text("\n");
        kernel_xhci_stop(&xhci_rt.Driver);
        xhci_rt.Ready = 0U;
    }
    if (xhci_rt.IrqEnabled) {
        if (xhci_rt.MsixCapOffset && xhci_rt.EcamRangeBase) {
            uint32_t ctrl = 0;
            if (ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
                    xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, xhci_rt.MsixCapOffset + 2U, 2U, 0, &ctrl)) {
                ctrl &= ~0x8000U;
                (void)ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
                    xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, xhci_rt.MsixCapOffset + 2U, 2U, 1, &ctrl);
            }
        }
        if (xhci_rt.MsiCapOffset && xhci_rt.EcamRangeBase) {
            uint32_t ctrl = 0;
            if (ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
                    xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, xhci_rt.MsiCapOffset + 2U, 2U, 0, &ctrl)) {
                ctrl &= ~0x0001U;
                (void)ecam_rw_private(xhci_rt.Window, xhci_rt.UcCacheBits, xhci_rt.EcamRangeBase,
                    xhci_rt.Bus, xhci_rt.Device, xhci_rt.Function, xhci_rt.MsiCapOffset + 2U, 2U, 1, &ctrl);
            }
        }
        if (xhci_rt.IrqToken.Id && x86_irq_registry())
            (void)x86_irq_registry_unbind(x86_irq_registry(), &xhci_rt.IrqToken);
        xhci_rt.IrqEnabled = 0U;
    }
}

// ====== x86_xhci_ready ======

int x86_xhci_ready(void)
{
    return xhci_rt.Self == (uintptr_t)&xhci_rt && xhci_rt.Ready;
}

// ====== x86_xhci_msc_cache ======

KERNEL_BLOCK_CACHE *x86_xhci_msc_cache(void)
{
    return (xhci_rt.Self == (uintptr_t)&xhci_rt && xhci_rt.Ready && xhci_rt.MscReady)
        ? &xhci_rt.MscCache
        : NULL;
}
