#include "direct.h"
#include "integration/kernel.h"
#include "kernel/drivers/video/virtio/discovery/pci.h"
#include "arch/x86/mm/heap/binding.h"
#include "arch/x86/mm/paging/heap/authority.h"
#include "arch/x86/configs/layout.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>

// VirtIO 1.2 Section 4.1.4.3: Common configuration MMIO offsets.
#define VGPU_COMMON_DEV_FEAT_SEL  0x00U
#define VGPU_COMMON_DEV_FEAT      0x04U
#define VGPU_COMMON_DRV_FEAT_SEL  0x08U
#define VGPU_COMMON_DRV_FEAT      0x0CU
#define VGPU_COMMON_STATUS        0x14U
#define VGPU_COMMON_Q_SELECT      0x16U
#define VGPU_COMMON_Q_SIZE        0x18U
#define VGPU_COMMON_Q_ENABLE      0x1CU
#define VGPU_COMMON_Q_NOTIFY_OFF  0x1EU
#define VGPU_COMMON_Q_DESC_LO     0x20U
#define VGPU_COMMON_Q_DESC_HI     0x24U
#define VGPU_COMMON_Q_AVAIL_LO    0x28U
#define VGPU_COMMON_Q_AVAIL_HI    0x2CU
#define VGPU_COMMON_Q_USED_LO     0x30U
#define VGPU_COMMON_Q_USED_HI     0x34U

// VirtIO 1.2 Section 2.1: Device status bits.
#define VGPU_STATUS_ACK           0x01U
#define VGPU_STATUS_DRIVER        0x02U
#define VGPU_STATUS_DRIVER_OK     0x04U
#define VGPU_STATUS_FEATURES_OK   0x08U

// VirtIO 1.2 Section 5.7.2: GPU virtqueue indices.
#define VGPU_Q_CONTROL            0U
#define VGPU_Q_CURSOR             1U
#define VGPU_RING_SIZE            8U

// VirtIO 1.2 Section 5.7.6.7 & 5.7.6.10: GPU 2D and Cursor command types.
#define VGPU_CMD_RESOURCE_CREATE_2D     0x0101U
#define VGPU_CMD_RESOURCE_UNREF         0x0102U
#define VGPU_CMD_SET_SCANOUT            0x0103U
#define VGPU_CMD_RESOURCE_FLUSH         0x0104U
#define VGPU_CMD_TRANSFER_TO_HOST_2D    0x0105U
#define VGPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VGPU_CMD_UPDATE_CURSOR          0x0300U
#define VGPU_CMD_MOVE_CURSOR            0x0301U
#define VGPU_RESP_OK_NODATA             0x1100U

// VirtIO 1.2 Section 5.7.6.7: Format codes.
#define VGPU_FORMAT_B8G8R8A8_UNORM      1U
#define VGPU_FORMAT_B8G8R8X8_UNORM      2U

// VirtIO 1.2 Section 2.7: Split virtqueue descriptor flags.
#define VRING_DESC_F_NEXT               1U
#define VRING_DESC_F_WRITE              2U

typedef struct {
    uint64_t  Addr;
    uint32_t  Len;
    uint16_t  Flags;
    uint16_t  Next;
} VGPU_DESC;

typedef struct {
    uint16_t  Flags;
    uint16_t  Idx;
    uint16_t  Ring[VGPU_RING_SIZE];
} VGPU_AVAIL;

typedef struct {
    uint32_t  Id;
    uint32_t  Len;
} VGPU_USED_ELEM;

typedef struct {
    uint16_t       Flags;
    uint16_t       Idx;
    VGPU_USED_ELEM Ring[VGPU_RING_SIZE];
} VGPU_DIR_USED;

typedef struct {
    VGPU_DESC     Desc[VGPU_RING_SIZE];
    VGPU_AVAIL    Avail;
    uint8_t       Pad[64 - sizeof(VGPU_AVAIL)];
    VGPU_DIR_USED Used;
} VGPU_SPLIT_RING;

typedef struct {
    volatile uint8_t  *Common;
    volatile uint8_t  *Notify;
    uint32_t           NotifyMult;
    uint16_t           CtrlNotifyOff;
    uint16_t           CurNotifyOff;
    VGPU_SPLIT_RING   *CtrlRing;
    uint64_t           CtrlRingPhys;
    uint8_t           *CtrlCmd;
    uint64_t           CtrlCmdPhys;
    uint8_t           *CtrlResp;
    uint64_t           CtrlRespPhys;
    uint16_t           CtrlAvailIdx;
    uint16_t           CtrlLastUsed;
    VGPU_SPLIT_RING   *CurRing;
    uint64_t           CurRingPhys;
    uint8_t           *CurCmd;
    uint64_t           CurCmdPhys;
    uint32_t          *CurPixels;
    uint64_t           CurPixelsPhys;
    uint16_t           CurAvailIdx;
    uint16_t           CurLastUsed;
    X86_PM_CLOCK       Clock;
    uint32_t           Ready;
    uint32_t           CursorReady;
    uint32_t           ScanoutBound;
    uint32_t           Width;
    uint32_t           Height;
    int32_t            CursorX;
    int32_t            CursorY;
} X86_VGPU_DIRECT;

static X86_VGPU_DIRECT vgpu_dir;

// ====== mmio_helpers ======

static uint8_t rd8(volatile uint8_t *base, uint32_t off) { return *(volatile uint8_t *)(base + off); }
static uint16_t rd16(volatile uint8_t *base, uint32_t off) { return *(volatile uint16_t *)(base + off); }
static uint32_t rd32(volatile uint8_t *base, uint32_t off) { return *(volatile uint32_t *)(base + off); }
static void wr8(volatile uint8_t *base, uint32_t off, uint8_t v) { *(volatile uint8_t *)(base + off) = v; }
static void wr16(volatile uint8_t *base, uint32_t off, uint16_t v) { *(volatile uint16_t *)(base + off) = v; }
static void wr32(volatile uint8_t *base, uint32_t off, uint32_t v) { *(volatile uint32_t *)(base + off) = v; }

static void fence_all(void)
{
    _ReadWriteBarrier();
    _mm_mfence();
    _ReadWriteBarrier();
}

static void put32(uint8_t *buf, uint32_t off, uint32_t v)
{
    buf[off + 0] = (uint8_t)(v & 0xFFU);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFFU);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFFU);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void put64(uint8_t *buf, uint32_t off, uint64_t v)
{
    put32(buf, off, (uint32_t)(v & 0xFFFFFFFFULL));
    put32(buf, off + 4U, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *buf, uint32_t off)
{
    return (uint32_t)buf[off]
        | ((uint32_t)buf[off + 1] << 8)
        | ((uint32_t)buf[off + 2] << 16)
        | ((uint32_t)buf[off + 3] << 24);
}

static void zero_bytes(void *ptr, uint32_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ====== setup_queue_private ======

static int setup_queue_private(uint16_t qidx, VGPU_SPLIT_RING *ring, uint64_t ring_phys, uint16_t *notify_off_out)
{
    wr16(vgpu_dir.Common, VGPU_COMMON_Q_SELECT, qidx);
    uint16_t max_sz = rd16(vgpu_dir.Common, VGPU_COMMON_Q_SIZE);
    if (max_sz < VGPU_RING_SIZE)
        return 0;
    zero_bytes(ring, sizeof(*ring));
    wr16(vgpu_dir.Common, VGPU_COMMON_Q_SIZE, VGPU_RING_SIZE);
    uint64_t desc_p = ring_phys + offsetof(VGPU_SPLIT_RING, Desc);
    uint64_t avail_p = ring_phys + offsetof(VGPU_SPLIT_RING, Avail);
    uint64_t used_p = ring_phys + offsetof(VGPU_SPLIT_RING, Used);
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_DESC_LO, (uint32_t)desc_p);
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_DESC_HI, (uint32_t)(desc_p >> 32));
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_AVAIL_LO, (uint32_t)avail_p);
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_AVAIL_HI, (uint32_t)(avail_p >> 32));
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_USED_LO, (uint32_t)used_p);
    wr32(vgpu_dir.Common, VGPU_COMMON_Q_USED_HI, (uint32_t)(used_p >> 32));
    *notify_off_out = rd16(vgpu_dir.Common, VGPU_COMMON_Q_NOTIFY_OFF);
    wr16(vgpu_dir.Common, VGPU_COMMON_Q_ENABLE, 1U);
    fence_all();
    return rd16(vgpu_dir.Common, VGPU_COMMON_Q_ENABLE) == 1U;
}

// ====== ctrl_exec_private ======

static int ctrl_exec_private(uint32_t cmd_len)
{
    zero_bytes(vgpu_dir.CtrlResp, 24U);
    vgpu_dir.CtrlRing->Desc[0].Addr = vgpu_dir.CtrlCmdPhys;
    vgpu_dir.CtrlRing->Desc[0].Len = cmd_len;
    vgpu_dir.CtrlRing->Desc[0].Flags = VRING_DESC_F_NEXT;
    vgpu_dir.CtrlRing->Desc[0].Next = 1U;
    vgpu_dir.CtrlRing->Desc[1].Addr = vgpu_dir.CtrlRespPhys;
    vgpu_dir.CtrlRing->Desc[1].Len = 24U;
    vgpu_dir.CtrlRing->Desc[1].Flags = VRING_DESC_F_WRITE;
    vgpu_dir.CtrlRing->Desc[1].Next = 0U;

    uint16_t slot = (uint16_t)(vgpu_dir.CtrlAvailIdx % VGPU_RING_SIZE);
    vgpu_dir.CtrlRing->Avail.Ring[slot] = 0U;
    fence_all();
    vgpu_dir.CtrlAvailIdx = (uint16_t)(vgpu_dir.CtrlAvailIdx + 1U);
    vgpu_dir.CtrlRing->Avail.Idx = vgpu_dir.CtrlAvailIdx;
    fence_all();

    wr16(vgpu_dir.Notify, (uint32_t)vgpu_dir.CtrlNotifyOff * vgpu_dir.NotifyMult, VGPU_Q_CONTROL);

    uint32_t start = x86_pm_read((unsigned short)vgpu_dir.Clock.Port) & vgpu_dir.Clock.Mask;
    uint32_t limit = 3579545U; // 1 second timeout
    for (;;) {
        fence_all();
        if (((volatile uint16_t *)&vgpu_dir.CtrlRing->Used.Idx)[0] != vgpu_dir.CtrlLastUsed) {
            vgpu_dir.CtrlLastUsed = ((volatile uint16_t *)&vgpu_dir.CtrlRing->Used.Idx)[0];
            break;
        }
        uint32_t now = x86_pm_read((unsigned short)vgpu_dir.Clock.Port) & vgpu_dir.Clock.Mask;
        if (((now - start) & vgpu_dir.Clock.Mask) >= limit)
            return 0;
        _mm_pause();
    }
    return get32(vgpu_dir.CtrlResp, 0U) == VGPU_RESP_OK_NODATA;
}

// ====== cursor_submit_private ======

static void cursor_submit_private(uint32_t cmd_type, uint32_t res_id, uint32_t x, uint32_t y)
{
    if (!vgpu_dir.CursorReady)
        return;
    // Drain any completed cursor descriptor before reusing slot 0.
    fence_all();
    uint32_t spins = 0;
    while (((volatile uint16_t *)&vgpu_dir.CurRing->Used.Idx)[0] != vgpu_dir.CurAvailIdx && spins < 200000U) {
        ++spins;
        _mm_pause();
    }
    vgpu_dir.CurLastUsed = ((volatile uint16_t *)&vgpu_dir.CurRing->Used.Idx)[0];

    zero_bytes(vgpu_dir.CurCmd, 56U);
    put32(vgpu_dir.CurCmd, 0U, cmd_type);
    put32(vgpu_dir.CurCmd, 24U, 0U);      // scanout_id = 0
    put32(vgpu_dir.CurCmd, 28U, x);       // pos.x
    put32(vgpu_dir.CurCmd, 32U, y);       // pos.y
    put32(vgpu_dir.CurCmd, 40U, res_id);  // resource_id (2 = cursor, 0 = hide)
    put32(vgpu_dir.CurCmd, 44U, 2U);      // hot_x
    put32(vgpu_dir.CurCmd, 48U, 2U);      // hot_y

    vgpu_dir.CurRing->Desc[0].Addr = vgpu_dir.CurCmdPhys;
    vgpu_dir.CurRing->Desc[0].Len = 56U;
    vgpu_dir.CurRing->Desc[0].Flags = 0U;
    vgpu_dir.CurRing->Desc[0].Next = 0U;

    uint16_t slot = (uint16_t)(vgpu_dir.CurAvailIdx % VGPU_RING_SIZE);
    vgpu_dir.CurRing->Avail.Ring[slot] = 0U;
    fence_all();
    vgpu_dir.CurAvailIdx = (uint16_t)(vgpu_dir.CurAvailIdx + 1U);
    vgpu_dir.CurRing->Avail.Idx = vgpu_dir.CurAvailIdx;
    fence_all();

    wr16(vgpu_dir.Notify, (uint32_t)vgpu_dir.CurNotifyOff * vgpu_dir.NotifyMult, VGPU_Q_CURSOR);
}

// ====== build_cursor_sprite_private ======

static void build_cursor_sprite_private(uint32_t *pixels)
{
    // Clean 16x18 rounded pointer arrow in top-left of 64x64 B8G8R8A8 cursor resource.
    static const char *const glyph[18] = {
        "##..............",
        "#W#.............",
        "#WW#............",
        "#WWW#...........",
        "#WWWW#..........",
        "#WWWWW#.........",
        "#WWWWWW#........",
        "#WWWWWWW#.......",
        "#WWWWWWWW#......",
        "#WWWWWWWWW#.....",
        "#WWWWWWWWWW#....",
        "#WWWWWW#####....",
        "#WWW#WW#........",
        "#WW#.#WW#.......",
        "#W#..#WW#.......",
        "##....#WW#......",
        "......#W#.......",
        ".......#........",
    };
    zero_bytes(pixels, 64U * 64U * 4U);
    for (uint32_t y = 0; y < 18U; ++y) {
        for (uint32_t x = 0; x < 16U; ++x) {
            char c = glyph[y][x];
            if (c == '#')
                pixels[y * 64U + x] = 0xFF20242CU;
            else if (c == 'W')
                pixels[y * 64U + x] = 0xFFFFFFFFU;
        }
    }
}

// ====== x86_vgpu_direct_start ======

int x86_vgpu_direct_start(const X86_PM_CLOCK *clock)
{
    X86_VGPU_MAPPING *map = x86_vgpu_kernel_mapping();
    if (!map || !map->Context || !map->Context->Info.Gpu.Present || !clock || !clock->Ready)
        return 1; // No VirtIO-GPU present: stay on UEFI GOP cleanly.

    KERNEL_VGPU_PCI_LAYOUT layout = {0};
    if (kernel_vgpu_pci_probe(map->Context->Info.Gpu.Config, 256U, map->Context->Info.Gpu.Sizes, 6U,
        map->Authority.PhysicalBits, &layout) != KERNEL_VGPU_PCI_READY)
        return 1;

    KERNEL_HEAP *heap = kernel_heap();
    const X86_HEAP_AUTH *heap_auth = x86_heap_owner();
    if (!heap || !heap_auth)
        return 1;

    // Allocate 6 contiguous 4 KiB pages: 0=CtrlRing+Cmd+Resp, 1=CurRing+Cmd, 2..5=64x64 cursor pixels (16 KiB).
    uint8_t *dma = (uint8_t *)kernel_heap_pages(heap, 6U);
    if (!dma)
        return 1;
    uintptr_t dma_virt = (uintptr_t)dma;
    uint64_t dma_phys = heap_auth->Data.Base + (uint64_t)(dma_virt - X86_HEAP_VIRTUAL);

    zero_bytes(&vgpu_dir, sizeof(vgpu_dir));
    vgpu_dir.Common = (volatile uint8_t *)(uintptr_t)(X86_GPU_VIRTUAL + map->Authority.Plan.Offset[0]);
    vgpu_dir.Notify = (volatile uint8_t *)(uintptr_t)(X86_GPU_VIRTUAL + map->Authority.Plan.Offset[1]);
    vgpu_dir.NotifyMult = layout.Regions[1].Multiplier;
    vgpu_dir.Clock = *clock;

    vgpu_dir.CtrlRing = (VGPU_SPLIT_RING *)dma;
    vgpu_dir.CtrlRingPhys = dma_phys;
    vgpu_dir.CtrlCmd = dma + 1024U;
    vgpu_dir.CtrlCmdPhys = dma_phys + 1024U;
    vgpu_dir.CtrlResp = dma + 2048U;
    vgpu_dir.CtrlRespPhys = dma_phys + 2048U;

    vgpu_dir.CurRing = (VGPU_SPLIT_RING *)(dma + 4096U);
    vgpu_dir.CurRingPhys = dma_phys + 4096U;
    vgpu_dir.CurCmd = dma + 4096U + 1024U;
    vgpu_dir.CurCmdPhys = dma_phys + 4096U + 1024U;

    vgpu_dir.CurPixels = (uint32_t *)(dma + 8192U);
    vgpu_dir.CurPixelsPhys = dma_phys + 8192U;

    // VirtIO 1.2 Section 3.1.1: Device Initialization Sequence.
    wr8(vgpu_dir.Common, VGPU_COMMON_STATUS, 0U);
    fence_all();
    wr8(vgpu_dir.Common, VGPU_COMMON_STATUS, VGPU_STATUS_ACK);
    wr8(vgpu_dir.Common, VGPU_COMMON_STATUS, VGPU_STATUS_ACK | VGPU_STATUS_DRIVER);

    // Negotiate VIRTIO_F_VERSION_1 (bit 32 = word 1 bit 0).
    wr32(vgpu_dir.Common, VGPU_COMMON_DEV_FEAT_SEL, 1U);
    uint32_t feat_hi = rd32(vgpu_dir.Common, VGPU_COMMON_DEV_FEAT);
    if ((feat_hi & 1U) == 0U)
        return 1;
    wr32(vgpu_dir.Common, VGPU_COMMON_DRV_FEAT_SEL, 0U);
    wr32(vgpu_dir.Common, VGPU_COMMON_DRV_FEAT, 0U);
    wr32(vgpu_dir.Common, VGPU_COMMON_DRV_FEAT_SEL, 1U);
    wr32(vgpu_dir.Common, VGPU_COMMON_DRV_FEAT, 1U);

    wr8(vgpu_dir.Common, VGPU_COMMON_STATUS, VGPU_STATUS_ACK | VGPU_STATUS_DRIVER | VGPU_STATUS_FEATURES_OK);
    fence_all();
    if ((rd8(vgpu_dir.Common, VGPU_COMMON_STATUS) & VGPU_STATUS_FEATURES_OK) == 0U)
        return 1;

    if (!setup_queue_private(VGPU_Q_CONTROL, vgpu_dir.CtrlRing, vgpu_dir.CtrlRingPhys, &vgpu_dir.CtrlNotifyOff))
        return 1;
    int has_cur = setup_queue_private(VGPU_Q_CURSOR, vgpu_dir.CurRing, vgpu_dir.CurRingPhys, &vgpu_dir.CurNotifyOff);

    wr8(vgpu_dir.Common, VGPU_COMMON_STATUS,
        VGPU_STATUS_ACK | VGPU_STATUS_DRIVER | VGPU_STATUS_FEATURES_OK | VGPU_STATUS_DRIVER_OK);
    fence_all();

    vgpu_dir.Ready = 1U;

    // Create 64x64 hardware cursor Resource ID 2 (VirtIO 1.2 Section 5.7.6.10).
    if (has_cur) {
        build_cursor_sprite_private(vgpu_dir.CurPixels);
        zero_bytes(vgpu_dir.CtrlCmd, 64U);
        put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_CREATE_2D);
        put32(vgpu_dir.CtrlCmd, 24U, 2U); // resource_id = 2
        put32(vgpu_dir.CtrlCmd, 28U, VGPU_FORMAT_B8G8R8A8_UNORM);
        put32(vgpu_dir.CtrlCmd, 32U, 64U);
        put32(vgpu_dir.CtrlCmd, 36U, 64U);
        if (ctrl_exec_private(40U)) {
            zero_bytes(vgpu_dir.CtrlCmd, 64U);
            put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_ATTACH_BACKING);
            put32(vgpu_dir.CtrlCmd, 24U, 2U); // resource_id = 2
            put32(vgpu_dir.CtrlCmd, 28U, 1U); // nr_entries = 1
            put64(vgpu_dir.CtrlCmd, 32U, vgpu_dir.CurPixelsPhys);
            put32(vgpu_dir.CtrlCmd, 40U, 64U * 64U * 4U);
            if (ctrl_exec_private(48U)) {
                zero_bytes(vgpu_dir.CtrlCmd, 64U);
                put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_TRANSFER_TO_HOST_2D);
                put32(vgpu_dir.CtrlCmd, 32U, 64U);
                put32(vgpu_dir.CtrlCmd, 36U, 64U);
                put64(vgpu_dir.CtrlCmd, 40U, 0ULL);
                put32(vgpu_dir.CtrlCmd, 48U, 2U);
                if (ctrl_exec_private(56U))
                    vgpu_dir.CursorReady = 1U;
            }
        }
    }

    (void)kernel_log(0, vgpu_dir.CursorReady ? "KERNEL_VGPU_2D_CURSOR_READY" : "KERNEL_VGPU_2D_READY");
    return 1;
}

// ====== x86_vgpu_direct_active ======

int x86_vgpu_direct_active(void) { return vgpu_dir.Ready != 0U; }

// ====== x86_vgpu_direct_has_cursor ======

int x86_vgpu_direct_has_cursor(void) { return vgpu_dir.Ready != 0U && vgpu_dir.CursorReady != 0U; }

// ====== x86_vgpu_direct_bind_backbuffer ======

int x86_vgpu_direct_bind_backbuffer(uint64_t phys_base, uint32_t width, uint32_t height)
{
    if (!vgpu_dir.Ready || !phys_base || !width || !height)
        return 0;
    if (vgpu_dir.ScanoutBound)
        x86_vgpu_direct_unbind_backbuffer();

    // 1) RESOURCE_CREATE_2D (resource_id = 1, B8G8R8X8_UNORM).
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_CREATE_2D);
    put32(vgpu_dir.CtrlCmd, 24U, 1U);
    put32(vgpu_dir.CtrlCmd, 28U, VGPU_FORMAT_B8G8R8X8_UNORM);
    put32(vgpu_dir.CtrlCmd, 32U, width);
    put32(vgpu_dir.CtrlCmd, 36U, height);
    if (!ctrl_exec_private(40U))
        return 0;

    // 2) RESOURCE_ATTACH_BACKING (zero-copy direct physical backbuffer lease!).
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_ATTACH_BACKING);
    put32(vgpu_dir.CtrlCmd, 24U, 1U);
    put32(vgpu_dir.CtrlCmd, 28U, 1U);
    put64(vgpu_dir.CtrlCmd, 32U, phys_base);
    put32(vgpu_dir.CtrlCmd, 40U, width * height * 4U);
    if (!ctrl_exec_private(48U))
        return 0;

    // 3) SET_SCANOUT (scanout 0 -> resource 1).
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_SET_SCANOUT);
    put32(vgpu_dir.CtrlCmd, 32U, width);
    put32(vgpu_dir.CtrlCmd, 36U, height);
    put32(vgpu_dir.CtrlCmd, 40U, 0U);
    put32(vgpu_dir.CtrlCmd, 44U, 1U);
    if (!ctrl_exec_private(48U))
        return 0;

    vgpu_dir.Width = width;
    vgpu_dir.Height = height;
    vgpu_dir.ScanoutBound = 1U;
    vgpu_dir.CursorX = (int32_t)(width / 2U);
    vgpu_dir.CursorY = (int32_t)(height / 2U);
    if (vgpu_dir.CursorReady)
        cursor_submit_private(VGPU_CMD_UPDATE_CURSOR, 2U, (uint32_t)vgpu_dir.CursorX, (uint32_t)vgpu_dir.CursorY);
    return 1;
}

// ====== x86_vgpu_direct_unbind_backbuffer ======

void x86_vgpu_direct_unbind_backbuffer(void)
{
    if (!vgpu_dir.Ready || !vgpu_dir.ScanoutBound)
        return;
    x86_vgpu_direct_cursor_hide();
    // Detach scanout 0 (resource_id = 0) and unref resource 1.
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_SET_SCANOUT);
    put32(vgpu_dir.CtrlCmd, 40U, 0U);
    put32(vgpu_dir.CtrlCmd, 44U, 0U);
    (void)ctrl_exec_private(48U);

    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_UNREF);
    put32(vgpu_dir.CtrlCmd, 24U, 1U);
    (void)ctrl_exec_private(32U);
    vgpu_dir.ScanoutBound = 0U;
}

// ====== x86_vgpu_direct_present_rect ======

int x86_vgpu_direct_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!vgpu_dir.Ready || !vgpu_dir.ScanoutBound)
        return 0;
    uint64_t offset = ((uint64_t)y * (uint64_t)vgpu_dir.Width + (uint64_t)x) * 4ULL;
    // 1) TRANSFER_TO_HOST_2D (DMA directly from backbuffer physical pages!).
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_TRANSFER_TO_HOST_2D);
    put32(vgpu_dir.CtrlCmd, 24U, x);
    put32(vgpu_dir.CtrlCmd, 28U, y);
    put32(vgpu_dir.CtrlCmd, 32U, width);
    put32(vgpu_dir.CtrlCmd, 36U, height);
    put64(vgpu_dir.CtrlCmd, 40U, offset);
    put32(vgpu_dir.CtrlCmd, 48U, 1U);
    if (!ctrl_exec_private(56U))
        return 0;

    // 2) RESOURCE_FLUSH.
    zero_bytes(vgpu_dir.CtrlCmd, 64U);
    put32(vgpu_dir.CtrlCmd, 0U, VGPU_CMD_RESOURCE_FLUSH);
    put32(vgpu_dir.CtrlCmd, 24U, x);
    put32(vgpu_dir.CtrlCmd, 28U, y);
    put32(vgpu_dir.CtrlCmd, 32U, width);
    put32(vgpu_dir.CtrlCmd, 36U, height);
    put32(vgpu_dir.CtrlCmd, 40U, 1U);
    return ctrl_exec_private(48U);
}

// ====== x86_vgpu_direct_cursor_set_pos ======

void x86_vgpu_direct_cursor_set_pos(int32_t x, int32_t y)
{
    if (!vgpu_dir.ScanoutBound || !vgpu_dir.CursorReady)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= vgpu_dir.Width) x = (int32_t)(vgpu_dir.Width - 1U);
    if ((uint32_t)y >= vgpu_dir.Height) y = (int32_t)(vgpu_dir.Height - 1U);
    vgpu_dir.CursorX = x;
    vgpu_dir.CursorY = y;
    cursor_submit_private(VGPU_CMD_MOVE_CURSOR, 2U, (uint32_t)x, (uint32_t)y);
}

// ====== x86_vgpu_direct_cursor_move_delta ======

void x86_vgpu_direct_cursor_move_delta(int32_t dx, int32_t dy)
{
    if (!vgpu_dir.ScanoutBound || !vgpu_dir.CursorReady)
        return;
    x86_vgpu_direct_cursor_set_pos(vgpu_dir.CursorX + dx, vgpu_dir.CursorY - dy);
}

// ====== x86_vgpu_direct_cursor_hide ======

void x86_vgpu_direct_cursor_hide(void)
{
    if (!vgpu_dir.CursorReady)
        return;
    cursor_submit_private(VGPU_CMD_UPDATE_CURSOR, 0U, 0U, 0U);
}
