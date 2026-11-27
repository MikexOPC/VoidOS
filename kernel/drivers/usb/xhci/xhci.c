#include "xhci.h"

// USB HID Usage Tables 1.12 §10 (Keyboard/Keypad Page 0x07) -> US-QWERTY ASCII.
static const uint8_t hid_usage_lower[0x39] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd', [0x08] = 'e',
    [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h', [0x0C] = 'i', [0x0D] = 'j',
    [0x0E] = 'k', [0x0F] = 'l', [0x10] = 'm', [0x11] = 'n', [0x12] = 'o',
    [0x13] = 'p', [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x', [0x1C] = 'y',
    [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4', [0x22] = '5',
    [0x23] = '6', [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
    [0x2C] = ' ', [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\',[0x33] = ';', [0x34] = '\'',[0x35] = '`', [0x36] = ',',
    [0x37] = '.', [0x38] = '/'
};

static const uint8_t hid_usage_upper[0x39] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D', [0x08] = 'E',
    [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H', [0x0C] = 'I', [0x0D] = 'J',
    [0x0E] = 'K', [0x0F] = 'L', [0x10] = 'M', [0x11] = 'N', [0x12] = 'O',
    [0x13] = 'P', [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X', [0x1C] = 'Y',
    [0x1D] = 'Z',
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$', [0x22] = '%',
    [0x23] = '^', [0x24] = '&', [0x25] = '*', [0x26] = '(', [0x27] = ')',
    [0x2C] = ' ', [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}',
    [0x31] = '|', [0x33] = ':', [0x34] = '"', [0x35] = '~', [0x36] = '<',
    [0x37] = '>', [0x38] = '?'
};

// ====== zero_mem_private ======

static void zero_mem_private(void *ptr, size_t bytes)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < bytes; ++i)
        p[i] = 0U;
}

// ====== delay_ms_private ======

static void delay_ms_private(const KERNEL_XHCI_TRANSPORT *t, uint32_t ms)
{
    if (!t || !t->ClockRead || !t->ClockHz)
        return;
    uint32_t start = 0, now = 0;
    if (!t->ClockRead(t->Context, &start))
        return;
    uint64_t target = ((uint64_t)ms * (uint64_t)t->ClockHz + 999ULL) / 1000ULL;
    if (target >= ((uint64_t)t->ClockMask >> 1))
        target = (uint64_t)t->ClockMask >> 2;
    for (uint32_t spin = 0; spin < 50000000U; ++spin) {
        if (!t->ClockRead(t->Context, &now))
            return;
        if (((now - start) & t->ClockMask) >= (uint32_t)target)
            return;
    }
}

// ====== kernel_xhci_delay_ms ======

void kernel_xhci_delay_ms(KERNEL_XHCI_DEVICE *dev, uint32_t ms)
{
    if (dev)
        delay_ms_private(&dev->Transport, ms);
}

// ====== wait_op_bits_private ======

static int wait_op_bits_private(KERNEL_XHCI_DEVICE *dev, uint32_t op_off,
    uint32_t mask, uint32_t expected, uint32_t timeout_ms)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t start = 0, now = 0, val = 0;
    if (!t->ClockRead(t->Context, &start))
        return 0;
    uint64_t limit = ((uint64_t)timeout_ms * (uint64_t)t->ClockHz + 999ULL) / 1000ULL;
    if (limit >= ((uint64_t)t->ClockMask >> 1))
        limit = (uint64_t)t->ClockMask >> 2;
    for (uint32_t spin = 0; spin < 50000000U; ++spin) {
        if (!t->Read32(t->Context, dev->CapLength + op_off, &val))
            return 0;
        if ((val & mask) == expected)
            return 1;
        if (!t->ClockRead(t->Context, &now))
            return 0;
        if (((now - start) & t->ClockMask) >= (uint32_t)limit)
            return 0;
    }
    return 0;
}

// ====== kernel_xhci_parse_hid_config ======

// USB 2.0 §9.6.3/9.6.5/9.6.6 & HID 1.11 §4.2: parse Configuration descriptor for Boot HID interface + Interrupt IN EP.
int kernel_xhci_parse_hid_config(const uint8_t *desc, uint32_t len, XHCI_HID_INFO *out)
{
    if (!desc || !out || len < 9U)
        return 0;
    if (desc[0] < 9U || desc[1] != USB_DESC_CONFIG)
        return 0;
    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > len)
        total = len;
    uint8_t config_val = desc[5];
    uint8_t cur_iface = 0;
    uint8_t cur_proto = 0;
    int in_boot_hid = 0;

    uint32_t pos = 0;
    while (pos + 2U <= total) {
        uint8_t b_len = desc[pos];
        uint8_t b_type = desc[pos + 1U];
        if (b_len < 2U || pos + b_len > total)
            break;
        if (b_type == USB_DESC_INTERFACE && b_len >= 9U) {
            uint8_t iclass = desc[pos + 5U];
            uint8_t isub = desc[pos + 6U];
            uint8_t iproto = desc[pos + 7U];
            if (iclass == USB_CLASS_HID && isub == USB_SUBCLASS_BOOT &&
                (iproto == USB_PROTO_KEYBOARD || iproto == USB_PROTO_MOUSE)) {
                cur_iface = desc[pos + 2U];
                cur_proto = iproto;
                in_boot_hid = 1;
            } else {
                in_boot_hid = 0;
            }
        } else if (b_type == USB_DESC_ENDPOINT && b_len >= 7U && in_boot_hid) {
            uint8_t ep_addr = desc[pos + 2U];
            uint8_t ep_attr = desc[pos + 3U];
            uint16_t max_pkt = (uint16_t)desc[pos + 4U] | ((uint16_t)desc[pos + 5U] << 8);
            uint8_t interval = desc[pos + 6U];
            // Interrupt IN endpoint: bit 7 of bEndpointAddress == 1, bmAttributes[1:0] == 3.
            if ((ep_addr & 0x80U) != 0U && (ep_attr & 0x03U) == 3U && max_pkt >= 3U) {
                out->ConfigValue = config_val;
                out->InterfaceNumber = cur_iface;
                out->Protocol = cur_proto;
                out->EndpointAddr = ep_addr;
                out->MaxPacketSize = max_pkt;
                out->Interval = interval ? interval : 10U;
                out->Reserved = 0U;
                return 1;
            }
        }
        pos += b_len;
    }
    return 0;
}

// ====== usage_to_scalar_private ======

static uint32_t usage_to_scalar_private(uint8_t usage, int shifted)
{
    if (usage == 0x28U) return 0x110003U; // Enter
    if (usage == 0x29U) return 0x1BU;     // Escape
    if (usage == 0x2AU) return 0x110001U; // Backspace
    if (usage == 0x2BU) return 0x110002U; // Tab
    if (usage < sizeof(hid_usage_lower))
        return shifted ? (uint32_t)hid_usage_upper[usage] : (uint32_t)hid_usage_lower[usage];
    return 0U;
}

// ====== key_in_report_private ======

static int key_in_report_private(const uint8_t report[8], uint8_t usage)
{
    for (uint32_t i = 2U; i < 8U; ++i) {
        if (report[i] == usage)
            return 1;
    }
    return 0;
}

// ====== kernel_xhci_decode_hid_kbd ======

// USB HID 1.11 Appendix B.1: 8-byte Boot Keyboard report [modifiers, reserved, key0..key5].
void kernel_xhci_decode_hid_kbd(uint8_t prev_report[8], const uint8_t report[8],
    XHCI_KEY_CALLBACK on_key, void *ctx)
{
    if (!prev_report || !report)
        return;
    // Usage 0x01 = ErrorRollOver: ignore invalid report without losing previous state.
    if (report[2] == 0x01U)
        return;
    uint8_t mod = report[0];
    int shifted = (mod & 0x22U) != 0U;
    uint32_t flags = 0U;
    if (shifted) flags |= XHCI_KEY_MOD_SHIFT;
    if (mod & 0x11U) flags |= XHCI_KEY_MOD_CTRL;
    if (mod & 0x44U) flags |= XHCI_KEY_MOD_ALT;

    // Emit releases for keys present in prev_report but absent in report.
    for (uint32_t i = 2U; i < 8U; ++i) {
        uint8_t old_u = prev_report[i];
        if (old_u >= 0x04U && !key_in_report_private(report, old_u)) {
            if (on_key)
                on_key(ctx, XHCI_KEY_EVENT_RELEASE | (uint32_t)old_u, 0U, flags);
        }
    }
    // Emit presses for keys present in report but absent in prev_report.
    for (uint32_t i = 2U; i < 8U; ++i) {
        uint8_t new_u = report[i];
        if (new_u >= 0x04U && !key_in_report_private(prev_report, new_u)) {
            uint32_t scalar = usage_to_scalar_private(new_u, shifted);
            if (on_key)
                on_key(ctx, (uint32_t)new_u, scalar, flags);
        }
    }
    for (uint32_t i = 0; i < 8U; ++i)
        prev_report[i] = report[i];
}

// ====== kernel_xhci_decode_hid_mouse ======

// USB HID 1.11 Appendix B.2: Boot Mouse report [buttons, int8 dx, int8 dy, (optional wheel)].
void kernel_xhci_decode_hid_mouse(const uint8_t *report, uint32_t len,
    XHCI_MOUSE_CALLBACK on_mouse, void *ctx)
{
    if (!report || len < 3U || !on_mouse)
        return;
    uint32_t buttons = (uint32_t)(report[0] & 0x07U);
    if (len >= 4U && report[3] != 0U)
        buttons |= ((uint32_t)report[3] << 8);
    int32_t dx = (int32_t)(int8_t)report[1];
    int32_t dy = (int32_t)(int8_t)report[2];
    on_mouse(ctx, dx, dy, buttons);
}

// ====== kernel_xhci_ring_doorbell ======

void kernel_xhci_ring_doorbell(KERNEL_XHCI_DEVICE *dev, uint32_t slot, uint32_t target)
{
    dev->Transport.Barrier(dev->Transport.Context);
    (void)dev->Transport.Write32(dev->Transport.Context, dev->DbOff + slot * 4U, target);
}

static void ring_doorbell_private(KERNEL_XHCI_DEVICE *dev, uint32_t slot, uint32_t target)
{
    kernel_xhci_ring_doorbell(dev, slot, target);
}

// ====== evt_ack_private ======

static void evt_ack_private(KERNEL_XHCI_DEVICE *dev)
{
    uint64_t erdp = dev->EvtRingPhys + ((uint64_t)dev->EvtDequeue * sizeof(XHCI_TRB));
    (void)dev->Transport.Write64(dev->Transport.Context,
        dev->RtsOff + XHCI_RT_IR0_BASE + XHCI_IR_ERDP, erdp | XHCI_ERDP_EHB);
    uint32_t sts = 0;
    if (dev->Transport.Read32(dev->Transport.Context, dev->CapLength + XHCI_OP_USBSTS, &sts) &&
        (sts & XHCI_STS_EINT)) {
        (void)dev->Transport.Write32(dev->Transport.Context,
            dev->CapLength + XHCI_OP_USBSTS, XHCI_STS_EINT);
    }
    uint32_t iman = 0;
    if (dev->Transport.Read32(dev->Transport.Context,
        dev->RtsOff + XHCI_RT_IR0_BASE + XHCI_IR_IMAN, &iman) && (iman & XHCI_IMAN_IP)) {
        (void)dev->Transport.Write32(dev->Transport.Context,
            dev->RtsOff + XHCI_RT_IR0_BASE + XHCI_IR_IMAN, iman | XHCI_IMAN_IP);
    }
}

// ====== cmd_submit_private ======

static int cmd_submit_private(KERNEL_XHCI_DEVICE *dev, uint64_t param, uint32_t status,
    uint32_t control, XHCI_TRB *event_out)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t idx = dev->CmdEnqueue;
    XHCI_TRB *trb = &dev->CmdRing[idx];
    trb->Parameter = param;
    trb->Status = status;
    t->Barrier(t->Context);
    trb->Control = (control & ~1U) | (uint32_t)dev->CmdCycle;

    ++dev->CmdEnqueue;
    if (dev->CmdEnqueue >= XHCI_RING_TRBS - 1U) {
        XHCI_TRB *link = &dev->CmdRing[XHCI_RING_TRBS - 1U];
        link->Parameter = dev->CmdRingPhys;
        link->Status = 0U;
        t->Barrier(t->Context);
        // Link TRB (type 6) with Toggle Cycle (bit 1).
        link->Control = (XHCI_TRB_LINK << 10) | (1U << 1) | (uint32_t)dev->CmdCycle;
        dev->CmdEnqueue = 0U;
        dev->CmdCycle ^= 1U;
    }
    ring_doorbell_private(dev, 0U, 0U);

    uint32_t start = 0, now = 0;
    if (!t->ClockRead(t->Context, &start))
        return 0;
    uint64_t limit = ((uint64_t)1000U * (uint64_t)t->ClockHz + 999ULL) / 1000ULL;
    if (limit >= ((uint64_t)t->ClockMask >> 1))
        limit = (uint64_t)t->ClockMask >> 2;
    for (uint32_t spin = 0; spin < 50000000U; ++spin) {
        t->Barrier(t->Context);
        XHCI_TRB ev = dev->EvtRing[dev->EvtDequeue];
        if ((ev.Control & 1U) == (uint32_t)dev->EvtCycle) {
            ++dev->EvtDequeue;
            if (dev->EvtDequeue >= XHCI_RING_TRBS) {
                dev->EvtDequeue = 0U;
                dev->EvtCycle ^= 1U;
            }
            evt_ack_private(dev);
            uint32_t type = (ev.Control >> 10) & 0x3FU;
            if (type == XHCI_TRB_CMD_COMPLETE) {
                if (event_out)
                    *event_out = ev;
                uint32_t cc = (ev.Status >> 24) & 0xFFU;
                return cc == XHCI_CC_SUCCESS;
            }
        }
        if (!t->ClockRead(t->Context, &now))
            return 0;
        if (((now - start) & t->ClockMask) >= (uint32_t)limit)
            return 0;
    }
    return 0;
}

// ====== kernel_xhci_submit_cmd ======

int kernel_xhci_submit_cmd(KERNEL_XHCI_DEVICE *dev, uint64_t param, uint32_t status,
    uint32_t control, XHCI_TRB *event_out)
{
    return cmd_submit_private(dev, param, status, control, event_out);
}

// ====== ep0_transfer_private ======

static int ep0_transfer_private(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id,
    XHCI_TRB *ep0_ring, uint64_t ep0_phys, uint32_t *ep0_enq, uint8_t *ep0_cycle,
    uint64_t setup_packet, uint64_t data_phys, uint32_t data_len, int dir_in)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    // Setup Stage TRB (§4.11.2.2): IDT (bit 6), TRT (bits 17:16): 0=No Data, 2=OUT, 3=IN.
    uint32_t trt = data_len ? (dir_in ? 3U : 2U) : 0U;
    XHCI_TRB *trb = &ep0_ring[*ep0_enq];
    trb->Parameter = setup_packet;
    trb->Status = 8U;
    t->Barrier(t->Context);
    trb->Control = (XHCI_TRB_SETUP << 10) | (1U << 6) | (trt << 16) | (uint32_t)(*ep0_cycle);
    *ep0_enq = (*ep0_enq + 1U) % (XHCI_RING_TRBS - 1U);

    if (data_len && data_phys) {
        trb = &ep0_ring[*ep0_enq];
        trb->Parameter = data_phys;
        trb->Status = data_len;
        t->Barrier(t->Context);
        trb->Control = (XHCI_TRB_DATA << 10) | ((uint32_t)(dir_in ? 1U : 0U) << 16) | (uint32_t)(*ep0_cycle);
        *ep0_enq = (*ep0_enq + 1U) % (XHCI_RING_TRBS - 1U);
    }

    // Status Stage TRB (§4.11.2.2): IOC (bit 5), DIR (bit 16) = opposite of data (or IN if no data).
    uint32_t status_dir = (data_len && dir_in) ? 0U : 1U;
    trb = &ep0_ring[*ep0_enq];
    trb->Parameter = 0ULL;
    trb->Status = 0U;
    t->Barrier(t->Context);
    trb->Control = (XHCI_TRB_STATUS << 10) | (1U << 5) | (status_dir << 16) | (uint32_t)(*ep0_cycle);
    *ep0_enq = (*ep0_enq + 1U) % (XHCI_RING_TRBS - 1U);
    if (*ep0_enq == 0U) {
        XHCI_TRB *link = &ep0_ring[XHCI_RING_TRBS - 1U];
        link->Parameter = ep0_phys;
        link->Status = 0U;
        t->Barrier(t->Context);
        link->Control = (XHCI_TRB_LINK << 10) | (1U << 1) | (uint32_t)(*ep0_cycle);
        *ep0_cycle ^= 1U;
    }

    ring_doorbell_private(dev, slot_id, 1U);

    uint32_t start = 0, now = 0;
    if (!t->ClockRead(t->Context, &start))
        return 0;
    uint64_t limit = ((uint64_t)1000U * (uint64_t)t->ClockHz + 999ULL) / 1000ULL;
    if (limit >= ((uint64_t)t->ClockMask >> 1))
        limit = (uint64_t)t->ClockMask >> 2;
    for (uint32_t spin = 0; spin < 50000000U; ++spin) {
        t->Barrier(t->Context);
        XHCI_TRB ev = dev->EvtRing[dev->EvtDequeue];
        if ((ev.Control & 1U) == (uint32_t)dev->EvtCycle) {
            ++dev->EvtDequeue;
            if (dev->EvtDequeue >= XHCI_RING_TRBS) {
                dev->EvtDequeue = 0U;
                dev->EvtCycle ^= 1U;
            }
            evt_ack_private(dev);
            uint32_t type = (ev.Control >> 10) & 0x3FU;
            uint32_t ev_slot = (ev.Control >> 24) & 0xFFU;
            if (type == XHCI_TRB_TRANSFER_EVT && ev_slot == slot_id) {
                uint32_t cc = (ev.Status >> 24) & 0xFFU;
                return cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET;
            }
        }
        if (!t->ClockRead(t->Context, &now))
            return 0;
        if (((now - start) & t->ClockMask) >= (uint32_t)limit)
            return 0;
    }
    return 0;
}

// ====== kernel_xhci_ep0_transfer ======

int kernel_xhci_ep0_transfer(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id,
    XHCI_TRB *ep0_ring, uint64_t ep0_phys, uint32_t *ep0_enq, uint8_t *ep0_cycle,
    uint64_t setup_pkt, uint64_t data_phys, uint32_t data_len, int dir_in)
{
    return ep0_transfer_private(dev, slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
        setup_pkt, data_phys, data_len, dir_in);
}

// ====== queue_hid_trb_private ======

static void queue_hid_trb_private(KERNEL_XHCI_DEVICE *dev, XHCI_HID_SLOT *hid)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t idx = hid->Enqueue;
    XHCI_TRB *trb = &hid->TransferRing[idx];
    trb->Parameter = hid->ReportBufPhys;
    trb->Status = (uint32_t)hid->MaxPacketSize;
    t->Barrier(t->Context);
    // Normal TRB (type 1), ISP (bit 2), IOC (bit 5).
    trb->Control = (XHCI_TRB_NORMAL << 10) | (1U << 2) | (1U << 5) | (uint32_t)hid->Cycle;

    ++hid->Enqueue;
    if (hid->Enqueue >= XHCI_RING_TRBS - 1U) {
        XHCI_TRB *link = &hid->TransferRing[XHCI_RING_TRBS - 1U];
        link->Parameter = hid->TransferRingPhys;
        link->Status = 0U;
        t->Barrier(t->Context);
        link->Control = (XHCI_TRB_LINK << 10) | (1U << 1) | (uint32_t)hid->Cycle;
        hid->Enqueue = 0U;
        hid->Cycle ^= 1U;
    }
    ring_doorbell_private(dev, hid->SlotId, hid->EpDci);
}

// ====== disable_slot_cmd_private ======

static void disable_slot_cmd_private(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id)
{
    if (!slot_id || slot_id > dev->MaxSlots)
        return;
    XHCI_TRB ev = {0};
    // xHCI Spec Rev. 1.2 §4.6.4 & §6.4.3.3: Disable Slot Command (TRB Type 10).
    (void)cmd_submit_private(dev, 0ULL, 0U, (XHCI_TRB_DISABLE_SLOT << 10) | (slot_id << 24), &ev);
    dev->Dcbaa[slot_id] = 0ULL;
    dev->Transport.Barrier(dev->Transport.Context);
}

// ====== find_hid_by_port_private ======

static XHCI_HID_SLOT *find_hid_by_port_private(KERNEL_XHCI_DEVICE *dev, uint32_t port_id)
{
    for (uint32_t i = 0; i < XHCI_MAX_HID_DEVICES; ++i) {
        if (dev->HidDevices[i].Active && dev->HidDevices[i].PortId == (uint8_t)port_id)
            return &dev->HidDevices[i];
    }
    return NULL;
}

// ====== alloc_hid_slot_private ======

static XHCI_HID_SLOT *alloc_hid_slot_private(KERNEL_XHCI_DEVICE *dev)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    for (uint32_t i = 0; i < XHCI_MAX_HID_DEVICES; ++i) {
        XHCI_HID_SLOT *hid = &dev->HidDevices[i];
        if (hid->Active)
            continue;
        if (!hid->DevCtx)
            hid->DevCtx = (uint8_t *)t->AllocDmaPage(t->Context, &hid->DevCtxPhys);
        if (!hid->Ep0Ring)
            hid->Ep0Ring = (XHCI_TRB *)t->AllocDmaPage(t->Context, &hid->Ep0RingPhys);
        if (!hid->TransferRing)
            hid->TransferRing = (XHCI_TRB *)t->AllocDmaPage(t->Context, &hid->TransferRingPhys);
        if (!hid->ReportBuf)
            hid->ReportBuf = (uint8_t *)t->AllocDmaPage(t->Context, &hid->ReportBufPhys);
        if (!hid->DevCtx || !hid->Ep0Ring || !hid->TransferRing || !hid->ReportBuf)
            return NULL;
        zero_mem_private(hid->DevCtx, 4096U);
        zero_mem_private(hid->Ep0Ring, 4096U);
        zero_mem_private(hid->TransferRing, 4096U);
        zero_mem_private(hid->ReportBuf, 4096U);
        zero_mem_private(hid->PrevReport, sizeof(hid->PrevReport));
        return hid;
    }
    return NULL;
}

// ====== kernel_xhci_enumerate_device ======

int kernel_xhci_enumerate_device(KERNEL_XHCI_DEVICE *dev, uint32_t root_port_idx,
    uint32_t speed, uint32_t route_string, uint32_t tt_info, int allow_hub)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    XHCI_HID_SLOT *hid = alloc_hid_slot_private(dev);
    if (!hid)
        return 0;

    // Default EP0 MaxPacketSize by xHCI §4.3: Low=8, Full=8 (or 64), High=64, Super=512.
    uint32_t ep0_mps = (speed == 4U) ? 512U : (speed == 3U ? 64U : 8U);

    // 1) Enable Slot Command (§4.6.3)
    XHCI_TRB ev = {0};
    if (!cmd_submit_private(dev, 0ULL, 0U, XHCI_TRB_ENABLE_SLOT << 10, &ev))
        return 0;
    uint32_t slot_id = (ev.Control >> 24) & 0xFFU;
    if (!slot_id || slot_id > dev->MaxSlots)
        return 0;

    uint8_t *in_ctx = dev->InCtx;
    uint64_t in_ctx_phys = dev->InCtxPhys;
    uint8_t *scratch = dev->ScratchBuf;
    uint64_t scratch_phys = dev->ScratchBufPhys;
    zero_mem_private(in_ctx, 4096U);
    zero_mem_private(scratch, 4096U);

    dev->Dcbaa[slot_id] = hid->DevCtxPhys;
    t->Barrier(t->Context);

    // 2) Address Device Command (§4.3.4 & §6.2.5): Add Flags = Slot (bit 0) | EP0 (bit 1).
    uint32_t csz = dev->ContextSize;
    uint32_t *icc = (uint32_t *)in_ctx;
    icc[1] = (1U << 0) | (1U << 1);
    uint32_t *slot_ctx = (uint32_t *)(in_ctx + csz);
    slot_ctx[0] = (1U << 27) | (speed << 20) | (route_string & 0xFFFFFU);
    slot_ctx[1] = (root_port_idx + 1U) << 16;
    slot_ctx[2] = tt_info;

    uint32_t *ep0_ctx = (uint32_t *)(in_ctx + 2U * csz);
    ep0_ctx[1] = (3U << 1) | (4U << 3) | (ep0_mps << 16); // CErr=3, EP Type=Control(4), MPS
    ep0_ctx[2] = (uint32_t)(hid->Ep0RingPhys | 1ULL);     // DCS = 1
    ep0_ctx[3] = (uint32_t)(hid->Ep0RingPhys >> 32);
    ep0_ctx[4] = 8U;                                      // Average TRB Length = 8

    if (!cmd_submit_private(dev, in_ctx_phys, 0U, (XHCI_TRB_ADDRESS_DEVICE << 10) | (slot_id << 24), &ev)) {
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    uint32_t ep0_enq = 0U;
    uint8_t ep0_cycle = 1U;

    // 3) GET_DESCRIPTOR (Configuration, index 0, 255 bytes): bmReq=0x80, bReq=6, wVal=0x0200, wIdx=0, wLen=255.
    uint64_t get_cfg = 0x80ULL | (6ULL << 8) | (0x0200ULL << 16) | (255ULL << 48);
    if (!ep0_transfer_private(dev, slot_id, hid->Ep0Ring, hid->Ep0RingPhys, &ep0_enq, &ep0_cycle,
        get_cfg, scratch_phys, 255U, 1)) {
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    XHCI_HID_INFO hid_info = {0};
    if (!kernel_xhci_parse_hid_config(scratch, 255U, &hid_info)) {
        XHCI_MSC_INFO msc_info = {0};
        if (dev->MscCount < XHCI_MAX_MSC_DEVICES && kernel_xhci_parse_msc_config(scratch, 255U, &msc_info)) {
            if (kernel_xhci_msc_setup(dev, slot_id, root_port_idx, speed, route_string, tt_info,
                hid->Ep0Ring, hid->Ep0RingPhys, &ep0_enq, &ep0_cycle,
                &msc_info, hid->DevCtx, hid->DevCtxPhys)) {
                hid->DevCtx = NULL;
                hid->DevCtxPhys = 0ULL;
                hid->Ep0Ring = NULL;
                hid->Ep0RingPhys = 0ULL;
                return 1;
            }
        }
        uint8_t hub_cfg = 0U;
        if (allow_hub && kernel_xhci_parse_hub_config(scratch, 255U, &hub_cfg)) {
            // Detach DevCtx & Ep0Ring from temporary HID slot before probing downstream devices.
            XHCI_TRB *hub_ep0 = hid->Ep0Ring;
            uint64_t hub_ep0_phys = hid->Ep0RingPhys;
            hid->DevCtx = NULL;
            hid->DevCtxPhys = 0ULL;
            hid->Ep0Ring = NULL;
            hid->Ep0RingPhys = 0ULL;
            return kernel_xhci_hub_probe(dev, slot_id, root_port_idx, speed, hub_cfg,
                hub_ep0, hub_ep0_phys, &ep0_enq, &ep0_cycle);
        }
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    // 4) SET_CONFIGURATION (bmReq=0x00, bReq=9, wVal=ConfigValue).
    uint64_t set_cfg = 0x00ULL | (9ULL << 8) | ((uint64_t)hid_info.ConfigValue << 16);
    if (!ep0_transfer_private(dev, slot_id, hid->Ep0Ring, hid->Ep0RingPhys, &ep0_enq, &ep0_cycle,
        set_cfg, 0ULL, 0U, 0)) {
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    // 5) HID SET_PROTOCOL (Boot Protocol = 0): bmReq=0x21, bReq=0x0B, wVal=0, wIdx=InterfaceNumber.
    uint64_t set_boot = 0x21ULL | (0x0BULL << 8) | ((uint64_t)hid_info.InterfaceNumber << 32);
    (void)ep0_transfer_private(dev, slot_id, hid->Ep0Ring, hid->Ep0RingPhys, &ep0_enq, &ep0_cycle,
        set_boot, 0ULL, 0U, 0);

    // 6) Configure Interrupt IN Endpoint (§4.6.6).
    uint32_t ep_num = (uint32_t)(hid_info.EndpointAddr & 0x0FU);
    uint32_t ep_dci = ep_num * 2U + 1U; // IN endpoint DCI = ep_num * 2 + 1
    if (ep_dci < 2U || ep_dci > 31U) {
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    zero_mem_private(in_ctx, 4096U);
    icc[1] = (1U << 0) | (1U << ep_dci);
    slot_ctx[0] = (ep_dci << 27) | (speed << 20) | (route_string & 0xFFFFFU);
    slot_ctx[1] = (root_port_idx + 1U) << 16;
    slot_ctx[2] = tt_info;

    // Interval encoding (§6.2.3.6): for FS/LS, exponent = log2(ms) + 3 (clamped 3..10).
    uint32_t interval_exp = 6U; // ~8 ms polling interval
    uint32_t *ep_ctx = (uint32_t *)(in_ctx + (ep_dci + 1U) * csz);
    ep_ctx[0] = interval_exp << 16;
    ep_ctx[1] = (3U << 1) | (7U << 3) | ((uint32_t)hid_info.MaxPacketSize << 16); // CErr=3, Interrupt IN(7)
    ep_ctx[2] = (uint32_t)(hid->TransferRingPhys | 1ULL); // DCS = 1
    ep_ctx[3] = (uint32_t)(hid->TransferRingPhys >> 32);
    ep_ctx[4] = (uint32_t)hid_info.MaxPacketSize | ((uint32_t)hid_info.MaxPacketSize << 16);

    if (!cmd_submit_private(dev, in_ctx_phys, 0U, (XHCI_TRB_CONFIGURE_EP << 10) | (slot_id << 24), &ev)) {
        disable_slot_cmd_private(dev, slot_id);
        return 0;
    }

    hid->Active = 1U;
    hid->SlotId = (uint8_t)slot_id;
    hid->PortId = (uint8_t)(root_port_idx + 1U);
    hid->Protocol = hid_info.Protocol;
    hid->EpDci = (uint8_t)ep_dci;
    hid->Cycle = 1U;
    hid->Enqueue = 0U;
    hid->MaxPacketSize = hid_info.MaxPacketSize > 64U ? 64U : hid_info.MaxPacketSize;
    ++dev->HidCount;

    // Prime initial Interrupt IN transfer.
    queue_hid_trb_private(dev, hid);
    return 1;
}

// ====== find_msc_by_port_private ======

static XHCI_MSC_SLOT *find_msc_by_port_private(KERNEL_XHCI_DEVICE *dev, uint32_t port_id)
{
    for (uint32_t i = 0; i < XHCI_MAX_MSC_DEVICES; ++i) {
        if (dev->Msc[i].Active && dev->Msc[i].PortId == (uint8_t)port_id)
            return &dev->Msc[i];
    }
    return NULL;
}

// ====== probe_port_private ======

static int probe_port_private(KERNEL_XHCI_DEVICE *dev, uint32_t port_idx)
{
    if (find_hid_by_port_private(dev, port_idx + 1U))
        return 0;
    if (find_msc_by_port_private(dev, port_idx + 1U))
        return 0;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t portsc_off = dev->CapLength + XHCI_OP_PORTSC_BASE + port_idx * XHCI_OP_PORT_STRIDE;
    uint32_t portsc = 0;
    if (!t->Read32(t->Context, portsc_off, &portsc) || !(portsc & XHCI_PORTSC_CCS))
        return 0;

    // Reset port (preserve PP, clear W1C change bits, set PR bit 4).
    uint32_t reset_val = (portsc & XHCI_PORTSC_PP) | XHCI_PORTSC_PR;
    if (!t->Write32(t->Context, portsc_off, reset_val))
        return 0;
    if (!wait_op_bits_private(dev, XHCI_OP_PORTSC_BASE + port_idx * XHCI_OP_PORT_STRIDE,
        XHCI_PORTSC_PR, 0U, 200U))
        return 0;
    delay_ms_private(t, 10U);
    if (!t->Read32(t->Context, portsc_off, &portsc) || !(portsc & XHCI_PORTSC_PED))
        return 0;
    // Clear PRC/CSC W1C bits without touching PED.
    (void)t->Write32(t->Context, portsc_off, (portsc & XHCI_PORTSC_PP) | XHCI_PORTSC_W1C_MASK);

    uint32_t speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    return kernel_xhci_enumerate_device(dev, port_idx, speed, 0U, 0U, 1);
}

// ====== handle_port_change_private ======

static void handle_port_change_private(KERNEL_XHCI_DEVICE *dev, uint32_t port_id)
{
    if (!port_id || port_id > dev->MaxPorts)
        return;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t port_idx = port_id - 1U;
    uint32_t portsc_off = dev->CapLength + XHCI_OP_PORTSC_BASE + port_idx * XHCI_OP_PORT_STRIDE;
    uint32_t portsc = 0;
    if (!t->Read32(t->Context, portsc_off, &portsc))
        return;

    // Clear W1C change bits while preserving PP and avoiding PED write-1-to-clear.
    (void)t->Write32(t->Context, portsc_off, (portsc & XHCI_PORTSC_PP) | XHCI_PORTSC_W1C_MASK);

    if ((portsc & XHCI_PORTSC_CCS) == 0U) {
        XHCI_HID_SLOT *hid = find_hid_by_port_private(dev, port_id);
        if (hid) {
            uint32_t old_slot = hid->SlotId;
            hid->Active = 0U;
            hid->SlotId = 0U;
            hid->PortId = 0U;
            zero_mem_private(hid->PrevReport, sizeof(hid->PrevReport));
            if (dev->HidCount > 0U)
                --dev->HidCount;
            disable_slot_cmd_private(dev, old_slot);
            ++dev->HotplugDisconnects;
        } else {
            XHCI_MSC_SLOT *msc = find_msc_by_port_private(dev, port_id);
            if (msc) {
                uint32_t old_slot = msc->SlotId;
                msc->Active = 0U;
                msc->SlotId = 0U;
                msc->PortId = 0U;
                if (dev->MscCount > 0U)
                    --dev->MscCount;
                disable_slot_cmd_private(dev, old_slot);
                ++dev->HotplugDisconnects;
            }
        }
    } else if (!find_hid_by_port_private(dev, port_id) &&
               !find_msc_by_port_private(dev, port_id)) {
        if (probe_port_private(dev, port_idx))
            ++dev->HotplugConnects;
    }
}

// ====== kernel_xhci_init ======

int kernel_xhci_init(KERNEL_XHCI_DEVICE *dev, const KERNEL_XHCI_TRANSPORT *transport)
{
    if (!dev || !transport || !transport->Read32 || !transport->Write32 || !transport->Write64 ||
        !transport->Barrier || !transport->AllocDmaPage || !transport->ClockRead || !transport->ClockHz)
        return 0;

    zero_mem_private(dev, sizeof(*dev));
    dev->Transport = *transport;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;

    uint32_t cap0 = 0, hcs1 = 0, hcs2 = 0, hcc1 = 0, dboff = 0, rtsoff = 0;
    if (!t->Read32(t->Context, XHCI_CAP_CAPLENGTH, &cap0) ||
        !t->Read32(t->Context, XHCI_CAP_HCSPARAMS1, &hcs1) ||
        !t->Read32(t->Context, XHCI_CAP_HCSPARAMS2, &hcs2) ||
        !t->Read32(t->Context, XHCI_CAP_HCCPARAMS1, &hcc1) ||
        !t->Read32(t->Context, XHCI_CAP_DBOFF, &dboff) ||
        !t->Read32(t->Context, XHCI_CAP_RTSOFF, &rtsoff))
        return 0;

    dev->CapLength = cap0 & 0xFFU;
    dev->MaxSlots = hcs1 & 0xFFU;
    dev->MaxPorts = (hcs1 >> 24) & 0xFFU;
    dev->ContextSize = (hcc1 & (1U << 2)) ? 64U : 32U;
    dev->DbOff = dboff & ~0x3U;
    dev->RtsOff = rtsoff & ~0x1FU;
    dev->ScratchpadCount = (((hcs2 >> 21) & 0x1FU) << 5) | ((hcs2 >> 27) & 0x1FU);

    if (dev->CapLength < 0x20U || !dev->MaxSlots || !dev->MaxPorts)
        return 0;
    if (dev->MaxSlots > 64U)
        dev->MaxSlots = 64U;

    // §4.22.1 BIOS -> OS Ownership Handoff via xECP Extended Capabilities.
    uint32_t xecp = ((hcc1 >> 16) & 0xFFFFU) << 2;
    for (uint32_t guard = 0; xecp && guard < 32U; ++guard) {
        uint32_t cap = 0;
        if (!t->Read32(t->Context, xecp, &cap))
            break;
        if ((cap & 0xFFU) == XHCI_EXT_CAP_LEGACY) {
            if (cap & XHCI_LEGSUP_BIOS_OWNED) {
                (void)t->Write32(t->Context, xecp, cap | XHCI_LEGSUP_OS_OWNED);
                for (uint32_t w = 0; w < 50U; ++w) {
                    if (!t->Read32(t->Context, xecp, &cap) || !(cap & XHCI_LEGSUP_BIOS_OWNED))
                        break;
                    delay_ms_private(t, 10U);
                }
            }
            break;
        }
        uint32_t next = (cap >> 8) & 0xFFU;
        if (!next)
            break;
        xecp += next << 2;
    }

    // §4.2 Host Controller Reset: halt (RS=0), wait HCH=1, set HCRST=1, wait HCRST=0 and CNR=0.
    uint32_t cmd = 0;
    if (!t->Read32(t->Context, dev->CapLength + XHCI_OP_USBCMD, &cmd))
        return 0;
    if (!t->Write32(t->Context, dev->CapLength + XHCI_OP_USBCMD, cmd & ~XHCI_CMD_RS))
        return 0;
    if (!wait_op_bits_private(dev, XHCI_OP_USBSTS, XHCI_STS_HCH, XHCI_STS_HCH, 200U))
        return 0;
    if (!t->Write32(t->Context, dev->CapLength + XHCI_OP_USBCMD, XHCI_CMD_HCRST))
        return 0;
    if (!wait_op_bits_private(dev, XHCI_OP_USBCMD, XHCI_CMD_HCRST, 0U, 500U))
        return 0;
    if (!wait_op_bits_private(dev, XHCI_OP_USBSTS, XHCI_STS_CNR, 0U, 500U))
        return 0;

    // Allocate DCBAA, Command Ring, Event Ring, ERST, reusable Input Context and Scratch Page.
    dev->Dcbaa = (uint64_t *)t->AllocDmaPage(t->Context, &dev->DcbaaPhys);
    dev->CmdRing = (XHCI_TRB *)t->AllocDmaPage(t->Context, &dev->CmdRingPhys);
    dev->EvtRing = (XHCI_TRB *)t->AllocDmaPage(t->Context, &dev->EvtRingPhys);
    dev->Erst = (XHCI_ERST_ENTRY *)t->AllocDmaPage(t->Context, &dev->ErstPhys);
    dev->InCtx = (uint8_t *)t->AllocDmaPage(t->Context, &dev->InCtxPhys);
    dev->ScratchBuf = (uint8_t *)t->AllocDmaPage(t->Context, &dev->ScratchBufPhys);
    if (!dev->Dcbaa || !dev->CmdRing || !dev->EvtRing || !dev->Erst || !dev->InCtx || !dev->ScratchBuf)
        return 0;
    zero_mem_private(dev->Dcbaa, 4096U);
    zero_mem_private(dev->CmdRing, 4096U);
    zero_mem_private(dev->EvtRing, 4096U);
    zero_mem_private(dev->Erst, 4096U);
    zero_mem_private(dev->InCtx, 4096U);
    zero_mem_private(dev->ScratchBuf, 4096U);

    // §4.20 Scratchpad Buffers (if requested by HCSPARAMS2).
    if (dev->ScratchpadCount > 0U && dev->ScratchpadCount <= 64U) {
        uint64_t sp_arr_phys = 0;
        uint64_t *sp_arr = (uint64_t *)t->AllocDmaPage(t->Context, &sp_arr_phys);
        if (!sp_arr)
            return 0;
        zero_mem_private(sp_arr, 4096U);
        for (uint32_t i = 0; i < dev->ScratchpadCount; ++i) {
            uint64_t p = 0;
            void *buf = t->AllocDmaPage(t->Context, &p);
            if (!buf)
                return 0;
            zero_mem_private(buf, 4096U);
            sp_arr[i] = p;
        }
        dev->Dcbaa[0] = sp_arr_phys;
    }

    dev->CmdEnqueue = 0U;
    dev->CmdCycle = 1U;
    dev->EvtDequeue = 0U;
    dev->EvtCycle = 1U;

    // Program CONFIG, DCBAAP, CRCR, and Interrupter 0 (ERSTSZ, ERDP, ERSTBA, IMOD, IMAN).
    if (!t->Write32(t->Context, dev->CapLength + XHCI_OP_CONFIG, dev->MaxSlots))
        return 0;
    if (!t->Write64(t->Context, dev->CapLength + XHCI_OP_DCBAAP, dev->DcbaaPhys))
        return 0;
    if (!t->Write64(t->Context, dev->CapLength + XHCI_OP_CRCR, dev->CmdRingPhys | 1ULL))
        return 0;

    dev->Erst[0].RingBase = dev->EvtRingPhys;
    dev->Erst[0].RingSize = XHCI_RING_TRBS;
    dev->Erst[0].Reserved = 0U;
    t->Barrier(t->Context);

    uint32_t ir0 = dev->RtsOff + XHCI_RT_IR0_BASE;
    if (!t->Write32(t->Context, ir0 + XHCI_IR_ERSTSZ, 1U) ||
        !t->Write64(t->Context, ir0 + XHCI_IR_ERDP, dev->EvtRingPhys) ||
        !t->Write64(t->Context, ir0 + XHCI_IR_ERSTBA, dev->ErstPhys) ||
        !t->Write32(t->Context, ir0 + XHCI_IR_IMOD, 0U) ||
        !t->Write32(t->Context, ir0 + XHCI_IR_IMAN, XHCI_IMAN_IP | XHCI_IMAN_IE))
        return 0;

    // Start controller (RS=1, INTE=1) and wait for HCH=0.
    if (!t->Write32(t->Context, dev->CapLength + XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE))
        return 0;
    if (!wait_op_bits_private(dev, XHCI_OP_USBSTS, XHCI_STS_HCH, 0U, 200U))
        return 0;

    // Enumerate connected root-hub ports for Boot HID keyboards & mice.
    for (uint32_t p = 0; p < dev->MaxPorts; ++p)
        (void)probe_port_private(dev, p);

    dev->Ready = 1U;
    return 1;
}

// ====== kernel_xhci_poll_events ======

uint32_t kernel_xhci_poll_events(KERNEL_XHCI_DEVICE *dev)
{
    if (!dev || !dev->Ready)
        return 0U;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t processed = 0U;
    uint64_t changed_ports = 0ULL;
    for (uint32_t budget = 0; budget < XHCI_RING_TRBS; ++budget) {
        t->Barrier(t->Context);
        XHCI_TRB ev = dev->EvtRing[dev->EvtDequeue];
        if ((ev.Control & 1U) != (uint32_t)dev->EvtCycle)
            break;
        ++dev->EvtDequeue;
        if (dev->EvtDequeue >= XHCI_RING_TRBS) {
            dev->EvtDequeue = 0U;
            dev->EvtCycle ^= 1U;
        }
        ++processed;
        uint32_t type = (ev.Control >> 10) & 0x3FU;
        if (type == XHCI_TRB_TRANSFER_EVT) {
            uint32_t slot_id = (ev.Control >> 24) & 0xFFU;
            uint32_t cc = (ev.Status >> 24) & 0xFFU;
            for (uint32_t i = 0; i < XHCI_MAX_HID_DEVICES; ++i) {
                XHCI_HID_SLOT *hid = &dev->HidDevices[i];
                if (hid->Active && hid->SlotId == slot_id) {
                    if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET) {
                        if (hid->Protocol == USB_PROTO_KEYBOARD) {
                            kernel_xhci_decode_hid_kbd(hid->PrevReport, hid->ReportBuf,
                                t->OnKey, t->Context);
                        } else if (hid->Protocol == USB_PROTO_MOUSE) {
                            kernel_xhci_decode_hid_mouse(hid->ReportBuf, hid->MaxPacketSize,
                                t->OnMouse, t->Context);
                        }
                        queue_hid_trb_private(dev, hid);
                    }
                    break;
                }
            }
        } else if (type == XHCI_TRB_PORT_CHANGE) {
            // xHCI Spec Rev. 1.2 §6.4.2.3: Parameter bits 31:24 hold 1-based Port ID.
            uint32_t port_id = (uint32_t)((ev.Parameter >> 24) & 0xFFU);
            if (port_id >= 1U && port_id <= 64U && port_id <= dev->MaxPorts)
                changed_ports |= (1ULL << (port_id - 1U));
        }
    }
    if (processed)
        evt_ack_private(dev);
    if (changed_ports) {
        for (uint32_t p = 0; p < dev->MaxPorts && p < 64U; ++p) {
            if (changed_ports & (1ULL << p))
                handle_port_change_private(dev, p + 1U);
        }
    }
    return processed;
}

// ====== kernel_xhci_stop ======

void kernel_xhci_stop(KERNEL_XHCI_DEVICE *dev)
{
    if (!dev || !dev->Ready)
        return;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    dev->Ready = 0U;
    (void)t->Write32(t->Context, dev->CapLength + XHCI_OP_USBCMD, 0U);
    (void)t->Write32(t->Context, dev->RtsOff + XHCI_RT_IR0_BASE + XHCI_IR_IMAN, XHCI_IMAN_IP);
    (void)wait_op_bits_private(dev, XHCI_OP_USBSTS, XHCI_STS_HCH, XHCI_STS_HCH, 200U);
}

// ====== kernel_xhci_wait_transfer ======

int kernel_xhci_wait_transfer(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id, uint32_t ep_dci, uint32_t timeout_ms)
{
    if (!dev || !slot_id)
        return 0;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    uint32_t start = 0, now = 0;
    if (!t->ClockRead(t->Context, &start))
        return 0;
    uint64_t limit = ((uint64_t)timeout_ms * (uint64_t)t->ClockHz + 999ULL) / 1000ULL;
    if (limit >= ((uint64_t)t->ClockMask >> 1))
        limit = (uint64_t)t->ClockMask >> 2;
    for (uint32_t spin = 0; spin < 50000000U; ++spin) {
        t->Barrier(t->Context);
        XHCI_TRB ev = dev->EvtRing[dev->EvtDequeue];
        if ((ev.Control & 1U) == (uint32_t)dev->EvtCycle) {
            ++dev->EvtDequeue;
            if (dev->EvtDequeue >= XHCI_RING_TRBS) {
                dev->EvtDequeue = 0U;
                dev->EvtCycle ^= 1U;
            }
            evt_ack_private(dev);
            uint32_t type = (ev.Control >> 10) & 0x3FU;
            if (type == XHCI_TRB_TRANSFER_EVT) {
                uint32_t ev_slot = (ev.Control >> 24) & 0xFFU;
                uint32_t ev_ep = (ev.Control >> 16) & 0x1FU;
                uint32_t cc = (ev.Status >> 24) & 0xFFU;
                if (ev_slot == slot_id && (ep_dci == 0U || ev_ep == ep_dci))
                    return cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET;
                for (uint32_t i = 0; i < XHCI_MAX_HID_DEVICES; ++i) {
                    XHCI_HID_SLOT *hid = &dev->HidDevices[i];
                    if (hid->Active && hid->SlotId == ev_slot) {
                        if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET) {
                            if (hid->Protocol == USB_PROTO_KEYBOARD) {
                                kernel_xhci_decode_hid_kbd(hid->PrevReport, hid->ReportBuf,
                                    t->OnKey, t->Context);
                            } else if (hid->Protocol == USB_PROTO_MOUSE) {
                                kernel_xhci_decode_hid_mouse(hid->ReportBuf, hid->MaxPacketSize,
                                    t->OnMouse, t->Context);
                            }
                            queue_hid_trb_private(dev, hid);
                        }
                        break;
                    }
                }
            }
        }
        if (!t->ClockRead(t->Context, &now))
            return 0;
        if (((now - start) & t->ClockMask) >= (uint32_t)limit)
            return 0;
    }
    return 0;
}
