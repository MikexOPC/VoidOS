#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/usb/xhci/xhci.h"

typedef struct {
    uint32_t  Count;
    uint32_t  Codes[16];
    uint32_t  Scalars[16];
    uint32_t  Flags[16];
} KEY_LOG;

typedef struct {
    uint32_t  Count;
    int32_t   Dx;
    int32_t   Dy;
    uint32_t  Buttons;
} MOUSE_LOG;

// ====== on_key_cb ======

static void on_key_cb(void *ctx, uint32_t code, uint32_t scalar, uint32_t flags)
{
    KEY_LOG *log = (KEY_LOG *)ctx;
    if (log->Count < 16U) {
        log->Codes[log->Count] = code;
        log->Scalars[log->Count] = scalar;
        log->Flags[log->Count] = flags;
        ++log->Count;
    }
}

// ====== on_mouse_cb ======

static void on_mouse_cb(void *ctx, int32_t dx, int32_t dy, uint32_t buttons)
{
    MOUSE_LOG *log = (MOUSE_LOG *)ctx;
    ++log->Count;
    log->Dx = dx;
    log->Dy = dy;
    log->Buttons = buttons;
}

// ====== test_parse_hid_descriptors ======

static void test_parse_hid_descriptors(void)
{
    // Realistic USB Boot Keyboard Configuration Descriptor (34 bytes).
    static const uint8_t kbd_cfg[] = {
        9, 2, 34, 0, 1, 1, 0, 0xA0, 50,             // Config (bConfigurationValue=1)
        9, 4, 0, 0, 1, 3, 1, 1, 0,                   // Interface 0: HID(3), Boot(1), Keyboard(1)
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,      // HID descriptor
        7, 5, 0x81, 0x03, 8, 0, 10                   // EP 0x81 IN, Interrupt, MPS=8, Interval=10
    };
    XHCI_HID_INFO info = {0};
    assert(kernel_xhci_parse_hid_config(kbd_cfg, sizeof(kbd_cfg), &info) == 1);
    assert(info.ConfigValue == 1U);
    assert(info.InterfaceNumber == 0U);
    assert(info.Protocol == USB_PROTO_KEYBOARD);
    assert(info.EndpointAddr == 0x81U);
    assert(info.MaxPacketSize == 8U);
    assert(info.Interval == 10U);

    // Realistic USB Boot Mouse Configuration Descriptor.
    static const uint8_t mouse_cfg[] = {
        9, 2, 34, 0, 1, 2, 0, 0xA0, 50,
        9, 4, 1, 0, 1, 3, 1, 2, 0,                   // Interface 1: HID(3), Boot(1), Mouse(2)
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 52, 0,
        7, 5, 0x82, 0x03, 4, 0, 8
    };
    memset(&info, 0, sizeof(info));
    assert(kernel_xhci_parse_hid_config(mouse_cfg, sizeof(mouse_cfg), &info) == 1);
    assert(info.ConfigValue == 2U);
    assert(info.InterfaceNumber == 1U);
    assert(info.Protocol == USB_PROTO_MOUSE);
    assert(info.EndpointAddr == 0x82U);
    assert(info.MaxPacketSize == 4U);

    // Truncated / non-HID descriptors must be rejected cleanly.
    static const uint8_t mass_storage[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 8, 6, 0x50, 0,
        7, 5, 0x81, 0x02, 64, 0, 0
    };
    assert(kernel_xhci_parse_hid_config(mass_storage, sizeof(mass_storage), &info) == 0);
    assert(kernel_xhci_parse_hid_config(kbd_cfg, 5U, &info) == 0);
}

// ====== test_hid_keyboard_decode ======

static void test_hid_keyboard_decode(void)
{
    uint8_t prev[8] = {0};
    KEY_LOG log = {0};

    // Press 'a' (usage 0x04), unshifted.
    uint8_t r1[8] = {0x00, 0, 0x04, 0, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r1, on_key_cb, &log);
    assert(log.Count == 1U);
    assert(log.Codes[0] == 0x04U);
    assert(log.Scalars[0] == 'a');
    assert(log.Flags[0] == 0U);

    // Hold 'a', press Shift + 'b' (usage 0x05) -> only 'B' press emitted.
    uint8_t r2[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r2, on_key_cb, &log);
    assert(log.Count == 2U);
    assert(log.Codes[1] == 0x05U);
    assert(log.Scalars[1] == 'B');
    assert((log.Flags[1] & XHCI_KEY_MOD_SHIFT) != 0U);

    // Rollover report (0x01) must be ignored without corrupting prev.
    uint8_t rollover[8] = {0x02, 0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    kernel_xhci_decode_hid_kbd(prev, rollover, on_key_cb, &log);
    assert(log.Count == 2U);

    // Release both 'a' and 'b', press Enter (0x28).
    uint8_t r3[8] = {0x00, 0, 0x28, 0, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r3, on_key_cb, &log);
    assert(log.Count == 5U);
    assert(log.Codes[2] == (XHCI_KEY_EVENT_RELEASE | 0x04U));
    assert(log.Codes[3] == (XHCI_KEY_EVENT_RELEASE | 0x05U));
    assert(log.Codes[4] == 0x28U);
    assert(log.Scalars[4] == 0x110003U);
}

// ====== test_hid_mouse_decode ======

static void test_hid_mouse_decode(void)
{
    MOUSE_LOG log = {0};
    uint8_t m1[3] = {0x01, 0xFB, 0x0A}; // Left button, dx=-5, dy=+10
    kernel_xhci_decode_hid_mouse(m1, sizeof(m1), on_mouse_cb, &log);
    assert(log.Count == 1U);
    assert(log.Buttons == 1U);
    assert(log.Dx == -5);
    assert(log.Dy == 10);

    // Short report (< 3 bytes) ignored safely.
    uint8_t bad[2] = {0x01, 0x02};
    kernel_xhci_decode_hid_mouse(bad, sizeof(bad), on_mouse_cb, &log);
    assert(log.Count == 1U);
}

// ====== Fake xHCI Controller for Hotplug Lifecycle Test ======

typedef struct {
    uint32_t  UsbCmd;
    uint32_t  UsbSts;
    uint32_t  PortSc;
    uint32_t  Clock;
    uint32_t  PagesAllocated;
    uint32_t  DisableSlotCalls;
    uint32_t  NextSlotId;
    uint32_t  EvtWriteIdx;
    uint8_t   EvtCycle;
    uint8_t   Pages[32][4096];
    XHCI_TRB *CmdRing;
    XHCI_TRB *EvtRing;
    uint32_t  CmdReadIdx;
    uint8_t   Ep0ReadIdx;
    KEY_LOG   Keys;
} FAKE_XHCI;

static const uint8_t fake_kbd_cfg[] = {
    9, 2, 34, 0, 1, 1, 0, 0xA0, 50,
    9, 4, 0, 0, 1, 3, 1, 1, 0,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
    7, 5, 0x81, 0x03, 8, 0, 10
};

static void fake_push_evt(FAKE_XHCI *f, uint64_t param, uint32_t status, uint32_t ctrl_no_cycle)
{
    XHCI_TRB *e = &f->EvtRing[f->EvtWriteIdx];
    e->Parameter = param;
    e->Status = status;
    e->Control = ctrl_no_cycle | (uint32_t)f->EvtCycle;
    ++f->EvtWriteIdx;
    if (f->EvtWriteIdx >= XHCI_RING_TRBS) {
        f->EvtWriteIdx = 0U;
        f->EvtCycle ^= 1U;
    }
}

static int fake_read32(void *ctx, uint32_t offset, uint32_t *val)
{
    FAKE_XHCI *f = (FAKE_XHCI *)ctx;
    if (offset == XHCI_CAP_CAPLENGTH) { *val = 0x01200020U; return 1; }
    if (offset == XHCI_CAP_HCSPARAMS1) { *val = (1U << 24) | 8U; return 1; } // 1 port, 8 slots
    if (offset == XHCI_CAP_HCSPARAMS2) { *val = 0U; return 1; }
    if (offset == XHCI_CAP_HCCPARAMS1) { *val = 0U; return 1; }
    if (offset == XHCI_CAP_DBOFF) { *val = 0x2000U; return 1; }
    if (offset == XHCI_CAP_RTSOFF) { *val = 0x1000U; return 1; }
    if (offset == 0x20U + XHCI_OP_USBCMD) { *val = f->UsbCmd; return 1; }
    if (offset == 0x20U + XHCI_OP_USBSTS) { *val = f->UsbSts; return 1; }
    if (offset == 0x20U + XHCI_OP_PORTSC_BASE) { *val = f->PortSc; return 1; }
    *val = 0U;
    return 1;
}

static int fake_write32(void *ctx, uint32_t offset, uint32_t val)
{
    FAKE_XHCI *f = (FAKE_XHCI *)ctx;
    if (offset == 0x20U + XHCI_OP_USBCMD) {
        if (val & XHCI_CMD_HCRST) {
            f->UsbCmd = 0U;
            f->UsbSts = XHCI_STS_HCH;
            return 1;
        }
        f->UsbCmd = val;
        f->UsbSts = (val & XHCI_CMD_RS) ? 0U : XHCI_STS_HCH;
        return 1;
    }
    if (offset == 0x20U + XHCI_OP_PORTSC_BASE) {
        if (val & XHCI_PORTSC_PR) {
            if (f->PortSc & XHCI_PORTSC_CCS)
                f->PortSc = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | XHCI_PORTSC_PED | (1U << XHCI_PORTSC_SPEED_SHIFT);
        } else {
            f->PortSc &= ~(val & XHCI_PORTSC_W1C_MASK);
        }
        return 1;
    }
    if (offset == 0x2000U) {
        // Command Doorbell (DB0)
        XHCI_TRB cmd = f->CmdRing[f->CmdReadIdx++];
        if (f->CmdReadIdx >= XHCI_RING_TRBS - 1U)
            f->CmdReadIdx = 0U;
        uint32_t type = (cmd.Control >> 10) & 0x3FU;
        uint32_t slot = (cmd.Control >> 24) & 0xFFU;
        if (type == XHCI_TRB_ENABLE_SLOT) {
            uint32_t new_slot = ++f->NextSlotId;
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24, (XHCI_TRB_CMD_COMPLETE << 10) | (new_slot << 24));
        } else if (type == XHCI_TRB_DISABLE_SLOT) {
            ++f->DisableSlotCalls;
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24, (XHCI_TRB_CMD_COMPLETE << 10) | (slot << 24));
        } else {
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24, (XHCI_TRB_CMD_COMPLETE << 10) | (slot << 24));
        }
        return 1;
    }
    if (offset > 0x2000U && offset < 0x2100U) {
        uint32_t slot = (offset - 0x2000U) / 4U;
        if (val == 1U) {
            // EP0 Doorbell: copy fake kbd config into ScratchBuf (page index 5).
            memcpy(f->Pages[5], fake_kbd_cfg, sizeof(fake_kbd_cfg));
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24, (XHCI_TRB_TRANSFER_EVT << 10) | (slot << 24));
        }
        return 1;
    }
    return 1;
}

static int fake_write64(void *ctx, uint32_t offset, uint64_t val)
{
    (void)ctx;
    (void)offset;
    (void)val;
    return 1;
}

static void fake_barrier(void *ctx)
{
    (void)ctx;
}

static void *fake_alloc_page(void *ctx, uint64_t *phys_out)
{
    FAKE_XHCI *f = (FAKE_XHCI *)ctx;
    if (f->PagesAllocated >= 32U)
        return NULL;
    uint32_t idx = f->PagesAllocated++;
    memset(f->Pages[idx], 0, 4096U);
    *phys_out = 0x100000ULL + (uint64_t)idx * 4096ULL;
    if (idx == 1U)
        f->CmdRing = (XHCI_TRB *)f->Pages[idx];
    if (idx == 2U)
        f->EvtRing = (XHCI_TRB *)f->Pages[idx];
    return f->Pages[idx];
}

static int fake_clock(void *ctx, uint32_t *ticks_out)
{
    FAKE_XHCI *f = (FAKE_XHCI *)ctx;
    f->Clock += 4000U;
    *ticks_out = f->Clock;
    return 1;
}

static void fake_on_key(void *ctx, uint32_t code, uint32_t scalar, uint32_t flags)
{
    FAKE_XHCI *f = (FAKE_XHCI *)ctx;
    on_key_cb(&f->Keys, code, scalar, flags);
}

// ====== test_hotplug_lifecycle ======

static void test_hotplug_lifecycle(void)
{
    static FAKE_XHCI fake;
    memset(&fake, 0, sizeof(fake));
    fake.UsbSts = XHCI_STS_HCH;
    fake.PortSc = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | (1U << XHCI_PORTSC_SPEED_SHIFT);
    fake.EvtCycle = 1U;

    KERNEL_XHCI_TRANSPORT transport = {
        &fake, 3579545U, 0xFFFFFFFFU,
        fake_read32, fake_write32, fake_write64,
        fake_barrier, fake_alloc_page, fake_clock,
        fake_on_key, NULL
    };

    KERNEL_XHCI_DEVICE dev;
    assert(kernel_xhci_init(&dev, &transport) == 1);
    assert(dev.HidCount == 1U);
    assert(dev.HidDevices[0].Active == 1U);
    assert(dev.HidDevices[0].Protocol == USB_PROTO_KEYBOARD);
    uint32_t pages_after_init = fake.PagesAllocated;

    // 1) Simulate disconnect on port 1 (CCS=0 + Port Status Change Event).
    fake.PortSc = XHCI_PORTSC_PP | XHCI_PORTSC_CSC;
    fake_push_evt(&fake, (1ULL << 24), XHCI_CC_SUCCESS << 24, XHCI_TRB_PORT_CHANGE << 10);
    assert(kernel_xhci_poll_events(&dev) == 1U);
    assert(dev.HidCount == 0U);
    assert(dev.HotplugDisconnects == 1U);
    assert(fake.DisableSlotCalls == 1U);

    // 2) Simulate reconnect on port 1 (CCS=1 + Port Status Change Event).
    fake.PortSc = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | XHCI_PORTSC_CSC | (1U << XHCI_PORTSC_SPEED_SHIFT);
    fake_push_evt(&fake, (1ULL << 24), XHCI_CC_SUCCESS << 24, XHCI_TRB_PORT_CHANGE << 10);
    assert(kernel_xhci_poll_events(&dev) == 1U);
    assert(dev.HidCount == 1U);
    assert(dev.HotplugConnects == 1U);
    assert(dev.HidDevices[0].Active == 1U);
    // Must reuse existing slot DMA pages with zero heap growth.
    assert(fake.PagesAllocated == pages_after_init);

    // 3) Deliver a key ('z' = usage 0x1D) on the reconnected slot.
    uint8_t z_rep[8] = {0, 0, 0x1D, 0, 0, 0, 0, 0};
    memcpy(dev.HidDevices[0].ReportBuf, z_rep, 8U);
    fake_push_evt(&fake, 0ULL, XHCI_CC_SUCCESS << 24,
        (XHCI_TRB_TRANSFER_EVT << 10) | ((uint32_t)dev.HidDevices[0].SlotId << 24));
    assert(kernel_xhci_poll_events(&dev) == 1U);
    assert(fake.Keys.Count == 1U);
    assert(fake.Keys.Scalars[0] == 'z');

    kernel_xhci_stop(&dev);
    assert(dev.Ready == 0U);
}

// ====== test_parse_msc_descriptor ======

static void test_parse_msc_descriptor(void)
{
    // Standard USB Mass Storage (Class 0x08, SubClass 0x06 SCSI, Proto 0x50 BOT) + Bulk IN 0x81 + Bulk OUT 0x02.
    static const uint8_t msc_cfg[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
        7, 5, 0x81, 0x02, 0x00, 0x02, 0, // Bulk IN 512
        7, 5, 0x02, 0x02, 0x00, 0x02, 0  // Bulk OUT 512
    };
    XHCI_MSC_INFO info = {0};
    assert(kernel_xhci_parse_msc_config(msc_cfg, sizeof(msc_cfg), &info) == 1);
    assert(info.ConfigValue == 1U);
    assert(info.BulkInAddr == 0x81U);
    assert(info.BulkOutAddr == 0x02U);
    assert(info.BulkInMps == 512U);
    assert(info.BulkOutMps == 512U);

    // Reject HID descriptor or missing Bulk OUT endpoint.
    assert(kernel_xhci_parse_msc_config(fake_kbd_cfg, sizeof(fake_kbd_cfg), &info) == 0);
    assert(kernel_xhci_parse_msc_config(msc_cfg, 25U, &info) == 0);
}

// ====== Fake xHCI MSC Controller for BOT + SCSI Read/Write Test ======

typedef struct {
    FAKE_XHCI  Base;
    uint8_t    Disk[16 * 512];
    uint32_t   LastTag;
    uint8_t    LastOp;
    uint32_t   LastLba;
    uint32_t   LastBlocks;
    int        CorruptNextCswTag;
    int        PendingDataOut;
    uint8_t   *CbwPtr;
    uint8_t   *CswPtr;
    uint8_t   *DataPtr;
} FAKE_MSC_XHCI;

static const uint8_t fake_msc_cfg[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
    7, 5, 0x81, 0x02, 0x00, 0x02, 0,
    7, 5, 0x02, 0x02, 0x00, 0x02, 0
};

static int msc_fake_write32(void *ctx, uint32_t offset, uint32_t val)
{
    FAKE_MSC_XHCI *m = (FAKE_MSC_XHCI *)ctx;
    FAKE_XHCI *f = &m->Base;
    if (offset > 0x2000U && offset < 0x2100U) {
        uint32_t slot = (offset - 0x2000U) / 4U;
        if (val == 1U) {
            memcpy(f->Pages[5], fake_msc_cfg, sizeof(fake_msc_cfg));
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24,
                (XHCI_TRB_TRANSFER_EVT << 10) | (1U << 16) | (slot << 24));
            return 1;
        }
        // Bulk OUT (DCI 4 = EP 2 OUT)
        if (val == 4U) {
            if (!m->PendingDataOut) {
                // CBW received
                assert(m->CbwPtr[0] == 'U' && m->CbwPtr[1] == 'S' && m->CbwPtr[2] == 'B' && m->CbwPtr[3] == 'C');
                m->LastTag = (uint32_t)m->CbwPtr[4] | ((uint32_t)m->CbwPtr[5] << 8) |
                             ((uint32_t)m->CbwPtr[6] << 16) | ((uint32_t)m->CbwPtr[7] << 24);
                m->LastOp = m->CbwPtr[15];
                m->LastLba = ((uint32_t)m->CbwPtr[17] << 24) | ((uint32_t)m->CbwPtr[18] << 16) |
                             ((uint32_t)m->CbwPtr[19] << 8) | (uint32_t)m->CbwPtr[20];
                m->LastBlocks = ((uint32_t)m->CbwPtr[22] << 8) | (uint32_t)m->CbwPtr[23];
                if (m->LastOp == 0x2AU)
                    m->PendingDataOut = 1;
            } else {
                // WRITE(10) Data OUT stage
                memcpy(&m->Disk[m->LastLba * 512U], m->DataPtr, m->LastBlocks * 512U);
                m->PendingDataOut = 0;
            }
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24,
                (XHCI_TRB_TRANSFER_EVT << 10) | (4U << 16) | (slot << 24));
            return 1;
        }
        // Bulk IN (DCI 3 = EP 1 IN)
        if (val == 3U) {
            if (m->LastOp == 0x25U) {
                // READ CAPACITY(10): 16 sectors -> last_lba = 15, block_size = 512
                memset(m->DataPtr, 0, 8U);
                m->DataPtr[3] = 15U;
                m->DataPtr[6] = 0x02U;
                m->LastOp = 0xFFU;
            } else if (m->LastOp == 0x28U) {
                // READ(10) Data IN stage
                memcpy(m->DataPtr, &m->Disk[m->LastLba * 512U], m->LastBlocks * 512U);
                m->LastOp = 0xFFU;
            } else {
                // CSW stage
                uint32_t tag = m->CorruptNextCswTag ? (m->LastTag ^ 0xDEADU) : m->LastTag;
                m->CorruptNextCswTag = 0;
                m->CswPtr[0] = 'U'; m->CswPtr[1] = 'S'; m->CswPtr[2] = 'B'; m->CswPtr[3] = 'S';
                m->CswPtr[4] = (uint8_t)(tag & 0xFFU);
                m->CswPtr[5] = (uint8_t)((tag >> 8) & 0xFFU);
                m->CswPtr[6] = (uint8_t)((tag >> 16) & 0xFFU);
                m->CswPtr[7] = (uint8_t)((tag >> 24) & 0xFFU);
                m->CswPtr[12] = 0U;
            }
            fake_push_evt(f, 0ULL, XHCI_CC_SUCCESS << 24,
                (XHCI_TRB_TRANSFER_EVT << 10) | (3U << 16) | (slot << 24));
            return 1;
        }
    }
    return fake_write32(f, offset, val);
}

static void test_msc_bot_read_write(void)
{
    static FAKE_MSC_XHCI m;
    memset(&m, 0, sizeof(m));
    m.Base.UsbSts = XHCI_STS_HCH;
    m.Base.PortSc = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | (3U << XHCI_PORTSC_SPEED_SHIFT);
    m.Base.EvtCycle = 1U;
    for (uint32_t i = 0; i < 512U; ++i)
        m.Disk[i] = (uint8_t)(i ^ 0xA5U);

    KERNEL_XHCI_TRANSPORT transport = {
        &m, 3579545U, 0xFFFFFFFFU,
        fake_read32, msc_fake_write32, fake_write64,
        fake_barrier, fake_alloc_page, fake_clock,
        NULL, NULL
    };

    // Pre-wire buffer pointers after alloc_page assigns indices 12(Cbw), 13(Csw), 14(Data).
    // Indices: 0=Dcbaa, 1=Cmd, 2=Evt, 3=Erst, 4=InCtx, 5=Scratch,
    // 6=DevCtx, 7=Ep0, 8=HidTr, 9=HidRep, 10=InRing, 11=OutRing, 12=Cbw, 13=Csw, 14=Data.
    m.CbwPtr = m.Base.Pages[12];
    m.CswPtr = m.Base.Pages[13];
    m.DataPtr = m.Base.Pages[14];

    KERNEL_XHCI_DEVICE dev;
    assert(kernel_xhci_init(&dev, &transport) == 1);
    assert(dev.MscCount == 1U);
    assert(dev.Msc[0].Active == 1U);
    assert(dev.Msc[0].BlockSize == 512U);
    assert(dev.Msc[0].TotalSectors == 16ULL);
    assert(dev.Msc[1].Active == 0U);

    uint8_t buf[512] = {0};
    assert(kernel_xhci_msc_read_sectors_at(&dev, 0U, 0ULL, 1U, buf) == 1);
    assert(memcmp(buf, m.Disk, 512U) == 0);
    assert(kernel_xhci_msc_read_sectors_at(&dev, 1U, 0ULL, 1U, buf) == 0);
    assert(kernel_xhci_msc_read_sectors_at(&dev, XHCI_MAX_MSC_DEVICES, 0ULL, 1U, buf) == 0);

    uint8_t pattern[512];
    for (uint32_t i = 0; i < 512U; ++i)
        pattern[i] = (uint8_t)(0x33U + i);
    assert(kernel_xhci_msc_write_sectors(&dev, 3ULL, 1U, pattern) == 1);
    assert(memcmp(&m.Disk[3U * 512U], pattern, 512U) == 0);

    // CSW tag mismatch must be rejected (prevents stale CSW spoofing).
    m.CorruptNextCswTag = 1;
    assert(kernel_xhci_msc_read_sectors(&dev, 3ULL, 1U, buf) == 0);

    // Out-of-range LBA must be rejected.
    assert(kernel_xhci_msc_read_sectors(&dev, 16ULL, 1U, buf) == 0);

    kernel_xhci_stop(&dev);
}

// ====== test_parse_hub_descriptor ======

static void test_parse_hub_descriptor(void)
{
    static const uint8_t hub_cfg[] = {
        9, 2, 25, 0, 1, 1, 0, 0xE0, 0,
        9, 4, 0, 0, 1, 0x09, 0x00, 0x00, 0, // Class 0x09 = Hub
        7, 5, 0x81, 0x03, 1, 0, 12
    };
    uint8_t cfg_val = 0U;
    assert(kernel_xhci_parse_hub_config(hub_cfg, sizeof(hub_cfg), &cfg_val) == 1);
    assert(cfg_val == 1U);
    assert(kernel_xhci_parse_hub_config(fake_kbd_cfg, sizeof(fake_kbd_cfg), &cfg_val) == 0);
    assert(kernel_xhci_parse_hub_config(fake_msc_cfg, sizeof(fake_msc_cfg), &cfg_val) == 0);
}

// ====== main ======

int main(void)
{
    test_parse_hid_descriptors();
    test_hid_keyboard_decode();
    test_hid_mouse_decode();
    test_hotplug_lifecycle();
    test_parse_msc_descriptor();
    test_msc_bot_read_write();
    test_parse_hub_descriptor();
    puts("xhci_host: PASS");
    return 0;
}
