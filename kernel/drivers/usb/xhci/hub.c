#include "xhci.h"

// USB 2.0 Specification §11.23.2.1 (Hub Descriptor 0x29), §11.24.2 (Hub Class Requests)
// & xHCI Specification Rev. 1.2 §6.2.2.2 (Slot Context for Hubs & TT).

#define USB_HUB_DESC_TYPE       0x29U
#define USB_HUB_FEAT_PORT_RESET 4U
#define USB_HUB_FEAT_PORT_POWER 8U
#define USB_HUB_FEAT_C_CONN     16U
#define USB_HUB_FEAT_C_RESET    20U

#define USB_HUB_STAT_CONN       (1U << 0)
#define USB_HUB_STAT_ENABLE     (1U << 1)
#define USB_HUB_STAT_RESET      (1U << 4)
#define USB_HUB_STAT_LOW_SPEED  (1U << 9)
#define USB_HUB_STAT_HIGH_SPEED (1U << 10)

// ====== zero_mem_private ======

static void zero_mem_private(void *ptr, size_t bytes)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < bytes; ++i)
        p[i] = 0U;
}

// ====== kernel_xhci_parse_hub_config ======

int kernel_xhci_parse_hub_config(const uint8_t *desc, uint32_t len, uint8_t *config_val_out)
{
    if (!desc || !config_val_out || len < 9U)
        return 0;
    if (desc[0] < 9U || desc[1] != USB_DESC_CONFIG)
        return 0;
    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > len)
        total = len;
    uint8_t cfg_val = desc[5];
    uint32_t pos = 0U;
    while (pos + 2U <= total) {
        uint8_t b_len = desc[pos];
        uint8_t b_type = desc[pos + 1U];
        if (b_len < 2U || pos + b_len > total)
            break;
        if (b_type == USB_DESC_INTERFACE && b_len >= 9U && desc[pos + 5U] == USB_CLASS_HUB) {
            *config_val_out = cfg_val;
            return 1;
        }
        pos += b_len;
    }
    return 0;
}

// ====== hub_get_port_status_private ======

static int hub_get_port_status_private(KERNEL_XHCI_DEVICE *dev, uint32_t hub_slot_id,
    XHCI_TRB *ep0_ring, uint64_t ep0_phys, uint32_t *ep0_enq, uint8_t *ep0_cycle,
    uint32_t port, uint16_t *status_out)
{
    // USB 2.0 §11.24.2.7: GET_PORT_STATUS (bmReq=0xA3, bReq=0, wVal=0, wIdx=port, wLen=4).
    uint64_t req = 0xA3ULL | ((uint64_t)port << 32) | (4ULL << 48);
    zero_mem_private(dev->ScratchBuf, 8U);
    if (!kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
        req, dev->ScratchBufPhys, 4U, 1))
        return 0;
    dev->Transport.Barrier(dev->Transport.Context);
    *status_out = (uint16_t)((uint16_t)dev->ScratchBuf[0] | ((uint16_t)dev->ScratchBuf[1] << 8));
    return 1;
}

// ====== kernel_xhci_hub_child_route ======

// xHCI 1.2 §6.2.2: Route String encodes 4-bit downstream port numbers (1..15) per tier (0..4).
uint32_t kernel_xhci_hub_child_route(uint32_t parent_route, uint32_t tier_depth, uint32_t port)
{
    if (tier_depth >= XHCI_MAX_HUB_TIERS || port == 0U || port > 15U)
        return 0U;
    return (parent_route & 0xFFFFFU) | ((port & 0xFU) << (tier_depth * 4U));
}

// ====== kernel_xhci_hub_child_tt ======

// xHCI 1.2 §6.2.2.2: preserve ancestor HS TT if already behind one, else set this HS hub as TT for LS/FS.
uint32_t kernel_xhci_hub_child_tt(uint32_t parent_tt, uint32_t hub_slot_id,
    uint32_t hub_speed, uint32_t dev_speed, uint32_t port)
{
    if (parent_tt != 0U)
        return parent_tt;
    if (hub_speed == 3U && (dev_speed == 1U || dev_speed == 2U))
        return (hub_slot_id & 0xFFU) | ((port & 0xFFU) << 8);
    return 0U;
}

// ====== kernel_xhci_hub_probe ======

int kernel_xhci_hub_probe(KERNEL_XHCI_DEVICE *dev, uint32_t hub_slot_id, uint32_t root_port_idx,
    uint32_t hub_speed, uint32_t hub_route_string, uint32_t hub_tt_info, uint32_t tier_depth,
    uint8_t config_val, XHCI_TRB *ep0_ring, uint64_t ep0_phys,
    uint32_t *ep0_enq, uint8_t *ep0_cycle)
{
    if (!dev || !hub_slot_id || !ep0_ring || tier_depth >= XHCI_MAX_HUB_TIERS)
        return 0;

    // 1) SET_CONFIGURATION on the hub.
    uint64_t set_cfg = 0x00ULL | (9ULL << 8) | ((uint64_t)config_val << 16);
    if (!kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
        set_cfg, 0ULL, 0U, 0))
        return 0;

    // 2) GET_DESCRIPTOR (Hub Descriptor 0x29, 9 bytes): bmReq=0xA0, bReq=6, wVal=0x2900, wLen=9.
    zero_mem_private(dev->ScratchBuf, 16U);
    uint64_t get_hub = 0xA0ULL | (6ULL << 8) | ((uint64_t)USB_HUB_DESC_TYPE << 24) | (9ULL << 48);
    if (!kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
        get_hub, dev->ScratchBufPhys, 9U, 1))
        return 0;
    dev->Transport.Barrier(dev->Transport.Context);
    if (dev->ScratchBuf[1] != USB_HUB_DESC_TYPE)
        return 0;
    uint32_t num_ports = dev->ScratchBuf[2];
    if (!num_ports)
        return 0;
    if (num_ports > 15U)
        num_ports = 15U; // Route String uses 4 bits per tier (1..15).

    // 3) Update Hub Slot Context via Evaluate Context (xHCI 1.2 §4.6.7 & §6.2.2.2): Hub=1, Number of Ports.
    uint32_t csz = dev->ContextSize;
    zero_mem_private(dev->InCtx, 4096U);
    uint32_t *icc = (uint32_t *)dev->InCtx;
    icc[1] = (1U << 0);
    uint32_t *slot_ctx = (uint32_t *)(dev->InCtx + csz);
    slot_ctx[0] = (1U << 27) | (1U << 26) | (hub_speed << 20) | (hub_route_string & 0xFFFFFU);
    slot_ctx[1] = ((root_port_idx + 1U) << 16) | (num_ports << 24);
    slot_ctx[2] = hub_tt_info;
    XHCI_TRB ev = {0};
    (void)kernel_xhci_submit_cmd(dev, dev->InCtxPhys, 0U,
        (XHCI_TRB_EVALUATE_CTX << 10) | (hub_slot_id << 24), &ev);

    // 4) Power on all downstream hub ports (SET_PORT_FEATURE PORT_POWER=8).
    for (uint32_t port = 1U; port <= num_ports; ++port) {
        uint64_t pwr = 0x23ULL | (3ULL << 8) | ((uint64_t)USB_HUB_FEAT_PORT_POWER << 16) | ((uint64_t)port << 32);
        (void)kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
            pwr, 0ULL, 0U, 0);
    }
    kernel_xhci_delay_ms(dev, 20U);

    // 5) Probe each connected downstream port.
    int found_any = 0;
    for (uint32_t port = 1U; port <= num_ports; ++port) {
        uint16_t status = 0U;
        if (!hub_get_port_status_private(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
            port, &status) || !(status & USB_HUB_STAT_CONN))
            continue;

        // Reset downstream port (SET_PORT_FEATURE PORT_RESET=4).
        uint64_t rst = 0x23ULL | (3ULL << 8) | ((uint64_t)USB_HUB_FEAT_PORT_RESET << 16) | ((uint64_t)port << 32);
        if (!kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
            rst, 0ULL, 0U, 0))
            continue;
        for (uint32_t wait = 0; wait < 20U; ++wait) {
            kernel_xhci_delay_ms(dev, 5U);
            if (!hub_get_port_status_private(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
                port, &status))
                break;
            if (!(status & USB_HUB_STAT_RESET))
                break;
        }
        if (!(status & USB_HUB_STAT_ENABLE))
            continue;

        // Clear C_PORT_RESET and C_PORT_CONNECTION change bits.
        uint64_t clr_rst = 0x23ULL | (1ULL << 8) | ((uint64_t)USB_HUB_FEAT_C_RESET << 16) | ((uint64_t)port << 32);
        uint64_t clr_con = 0x23ULL | (1ULL << 8) | ((uint64_t)USB_HUB_FEAT_C_CONN << 16) | ((uint64_t)port << 32);
        (void)kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle, clr_rst, 0ULL, 0U, 0);
        (void)kernel_xhci_ep0_transfer(dev, hub_slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle, clr_con, 0ULL, 0U, 0);

        uint32_t dev_speed = 1U; // Full Speed default
        if (status & USB_HUB_STAT_LOW_SPEED)
            dev_speed = 2U;
        else if (status & USB_HUB_STAT_HIGH_SPEED)
            dev_speed = 3U;

        uint32_t child_route = kernel_xhci_hub_child_route(hub_route_string, tier_depth, port);
        uint32_t child_tt = kernel_xhci_hub_child_tt(hub_tt_info, hub_slot_id, hub_speed, dev_speed, port);

        if (child_route && kernel_xhci_enumerate_device(dev, root_port_idx, dev_speed,
            child_route, child_tt, tier_depth + 1U))
            found_any = 1;
    }

    return found_any;
}
