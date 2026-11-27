#ifndef OS_KERNEL_DRIVERS_USB_XHCI_H
#define OS_KERNEL_DRIVERS_USB_XHCI_H

#include <stddef.h>
#include <stdint.h>

// xHCI Specification Rev. 1.2 (May 2019) register offsets and bit definitions.

// §5.3 Host Controller Capability Registers
#define XHCI_CAP_CAPLENGTH      0x00U
#define XHCI_CAP_HCSPARAMS1     0x04U
#define XHCI_CAP_HCSPARAMS2     0x08U
#define XHCI_CAP_HCCPARAMS1     0x10U
#define XHCI_CAP_DBOFF          0x14U
#define XHCI_CAP_RTSOFF         0x18U

// §5.4 Host Controller Operational Registers (relative to CapLength)
#define XHCI_OP_USBCMD          0x00U
#define XHCI_OP_USBSTS          0x04U
#define XHCI_OP_PAGESIZE        0x08U
#define XHCI_OP_DNCTRL          0x14U
#define XHCI_OP_CRCR            0x18U
#define XHCI_OP_DCBAAP          0x30U
#define XHCI_OP_CONFIG          0x38U
#define XHCI_OP_PORTSC_BASE     0x400U
#define XHCI_OP_PORT_STRIDE     0x10U

#define XHCI_CMD_RS             (1U << 0)
#define XHCI_CMD_HCRST          (1U << 1)
#define XHCI_CMD_INTE           (1U << 2)
#define XHCI_CMD_HSEE           (1U << 3)

#define XHCI_STS_HCH            (1U << 0)
#define XHCI_STS_HSE            (1U << 2)
#define XHCI_STS_EINT           (1U << 3)
#define XHCI_STS_PCD            (1U << 4)
#define XHCI_STS_CNR            (1U << 11)

// §5.4.8 Port Status and Control Register (PORTSC)
#define XHCI_PORTSC_CCS         (1U << 0)
#define XHCI_PORTSC_PED         (1U << 1)
#define XHCI_PORTSC_PR          (1U << 4)
#define XHCI_PORTSC_PP          (1U << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10U
#define XHCI_PORTSC_SPEED_MASK  (0xFU << 10)
#define XHCI_PORTSC_CSC         (1U << 17)
#define XHCI_PORTSC_PEC         (1U << 18)
#define XHCI_PORTSC_WRC         (1U << 19)
#define XHCI_PORTSC_OCC         (1U << 20)
#define XHCI_PORTSC_PRC         (1U << 21)
#define XHCI_PORTSC_PLC         (1U << 22)
#define XHCI_PORTSC_CEC         (1U << 23)
#define XHCI_PORTSC_W1C_MASK    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | \
                                 XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)

// §5.5.2 Interrupter Register Set 0 (relative to RTSOFF + 0x20)
#define XHCI_RT_IR0_BASE        0x20U
#define XHCI_IR_IMAN            0x00U
#define XHCI_IR_IMOD            0x04U
#define XHCI_IR_ERSTSZ          0x08U
#define XHCI_IR_ERSTBA          0x10U
#define XHCI_IR_ERDP            0x18U
#define XHCI_IMAN_IP            (1U << 0)
#define XHCI_IMAN_IE            (1U << 1)
#define XHCI_ERDP_EHB           (1ULL << 3)

// §7.1 Extended Capabilities (USB Legacy Support)
#define XHCI_EXT_CAP_LEGACY     1U
#define XHCI_LEGSUP_BIOS_OWNED  (1U << 16)
#define XHCI_LEGSUP_OS_OWNED    (1U << 24)

// §6.4.6 TRB Types
#define XHCI_TRB_NORMAL         1U
#define XHCI_TRB_SETUP          2U
#define XHCI_TRB_DATA           3U
#define XHCI_TRB_STATUS         4U
#define XHCI_TRB_LINK           6U
#define XHCI_TRB_ENABLE_SLOT    9U
#define XHCI_TRB_DISABLE_SLOT   10U
#define XHCI_TRB_ADDRESS_DEVICE 11U
#define XHCI_TRB_CONFIGURE_EP   12U
#define XHCI_TRB_EVALUATE_CTX   13U
#define XHCI_TRB_TRANSFER_EVT   32U
#define XHCI_TRB_CMD_COMPLETE   33U
#define XHCI_TRB_PORT_CHANGE    34U

// §6.4.5 TRB Completion Codes
#define XHCI_CC_SUCCESS         1U
#define XHCI_CC_SHORT_PACKET    13U

// USB 2.0 / HID 1.11 Constants
#define USB_DESC_DEVICE         1U
#define USB_DESC_CONFIG         2U
#define USB_DESC_INTERFACE      4U
#define USB_DESC_ENDPOINT       5U
#define USB_DESC_HID            0x21U
#define USB_DESC_HID_REPORT     0x22U
#define USB_CLASS_HID           3U
#define USB_SUBCLASS_BOOT       1U
#define USB_PROTO_KEYBOARD      1U
#define USB_PROTO_MOUSE         2U
#define USB_PROTO_REPORT_PTR    3U
#define USB_CLASS_MSC           8U
#define USB_SUBCLASS_SCSI       6U
#define USB_PROTO_BOT           0x50U
#define USB_CLASS_HUB           9U

#define XHCI_RING_TRBS          64U
#define XHCI_MAX_HID_DEVICES    16U
// Up to 4 simultaneous USB Mass Storage drives (/usb, /usb1, /usb2, /usb3) within KERNEL_VFS_MAX_MOUNTS (8).
#define XHCI_MAX_MSC_DEVICES    4U
// xHCI 1.2 §6.2.2: Route String is 20 bits (5 tiers x 4 bits, ports 1..15).
#define XHCI_MAX_HUB_TIERS      5U

#define XHCI_KEY_MOD_SHIFT      (1U << 0)
#define XHCI_KEY_MOD_CTRL       (1U << 1)
#define XHCI_KEY_MOD_ALT        (1U << 2)
#define XHCI_KEY_EVENT_RELEASE  0x00020000U

typedef struct {
    uint64_t  Parameter;
    uint32_t  Status;
    uint32_t  Control;
} XHCI_TRB;

typedef struct {
    uint64_t  RingBase;
    uint32_t  RingSize;
    uint32_t  Reserved;
} XHCI_ERST_ENTRY;

_Static_assert(sizeof(XHCI_TRB) == 16U, "xHCI TRB ABI must be 16 bytes");
_Static_assert(sizeof(XHCI_ERST_ENTRY) == 16U, "xHCI ERST entry ABI must be 16 bytes");

typedef struct {
    uint8_t   ConfigValue;
    uint8_t   InterfaceNumber;
    uint8_t   SubClass;
    uint8_t   Protocol;
    uint8_t   EndpointAddr;
    uint8_t   Interval;
    uint16_t  MaxPacketSize;
    uint16_t  ReportDescLen;
    uint16_t  Reserved;
} XHCI_HID_INFO;

typedef struct {
    uint8_t   Valid;
    uint8_t   IsAbsolute;
    uint8_t   HasReportId;
    uint8_t   ReportId;
    uint16_t  ButtonsBit;
    uint8_t   ButtonsCount;
    uint8_t   WheelBits;
    uint16_t  XBit;
    uint8_t   XBits;
    uint8_t   YBits;
    uint16_t  YBit;
    uint16_t  WheelBit;
    int32_t   XMin;
    int32_t   XMax;
    int32_t   YMin;
    int32_t   YMax;
} XHCI_HID_REPORT_LAYOUT;

typedef struct {
    uint8_t   Active;
    uint8_t   SlotId;
    uint8_t   PortId;
    uint8_t   Protocol;
    uint8_t   EpDci;
    uint8_t   Cycle;
    uint16_t  MaxPacketSize;
    uint32_t  Enqueue;
    uint8_t  *DevCtx;
    uint64_t  DevCtxPhys;
    XHCI_TRB *Ep0Ring;
    uint64_t  Ep0RingPhys;
    XHCI_TRB *TransferRing;
    uint64_t  TransferRingPhys;
    uint8_t  *ReportBuf;
    uint64_t  ReportBufPhys;
    uint8_t   PrevReport[8];
    XHCI_HID_REPORT_LAYOUT ReportLayout;
} XHCI_HID_SLOT;

typedef struct {
    uint8_t   ConfigValue;
    uint8_t   InterfaceNumber;
    uint8_t   BulkInAddr;
    uint8_t   BulkOutAddr;
    uint16_t  BulkInMps;
    uint16_t  BulkOutMps;
} XHCI_MSC_INFO;

typedef struct {
    uint8_t   Active;
    uint8_t   SlotId;
    uint8_t   PortId;
    uint8_t   InDci;
    uint8_t   OutDci;
    uint8_t   InCycle;
    uint8_t   OutCycle;
    uint8_t   Reserved;
    uint32_t  InEnqueue;
    uint32_t  OutEnqueue;
    uint32_t  BlockSize;
    uint32_t  Tag;
    uint64_t  TotalSectors;
    uint8_t  *DevCtx;
    uint64_t  DevCtxPhys;
    XHCI_TRB *Ep0Ring;
    uint64_t  Ep0RingPhys;
    XHCI_TRB *InRing;
    uint64_t  InRingPhys;
    XHCI_TRB *OutRing;
    uint64_t  OutRingPhys;
    uint8_t  *CbwBuf;
    uint64_t  CbwBufPhys;
    uint8_t  *CswBuf;
    uint64_t  CswBufPhys;
    uint8_t  *DataBuf;
    uint64_t  DataBufPhys;
} XHCI_MSC_SLOT;

typedef void (*XHCI_KEY_CALLBACK)(void *ctx, uint32_t code, uint32_t scalar, uint32_t flags);
typedef void (*XHCI_MOUSE_CALLBACK)(void *ctx, int32_t dx, int32_t dy, uint32_t buttons);
typedef void (*XHCI_ABS_MOUSE_CALLBACK)(void *ctx, int32_t abs_x, int32_t abs_y,
    int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y, uint32_t buttons);

typedef struct {
    void     *Context;
    uint32_t  ClockHz;
    uint32_t  ClockMask;
    int     (*Read32)(void *ctx, uint32_t offset, uint32_t *val);
    int     (*Write32)(void *ctx, uint32_t offset, uint32_t val);
    int     (*Write64)(void *ctx, uint32_t offset, uint64_t val);
    void    (*Barrier)(void *ctx);
    void   *(*AllocDmaPage)(void *ctx, uint64_t *phys_out);
    int     (*ClockRead)(void *ctx, uint32_t *ticks_out);
    XHCI_KEY_CALLBACK       OnKey;
    XHCI_MOUSE_CALLBACK     OnMouse;
    XHCI_ABS_MOUSE_CALLBACK OnAbsMouse;
} KERNEL_XHCI_TRANSPORT;

typedef struct {
    KERNEL_XHCI_TRANSPORT  Transport;
    uint32_t               CapLength;
    uint32_t               DbOff;
    uint32_t               RtsOff;
    uint32_t               MaxSlots;
    uint32_t               MaxPorts;
    uint32_t               ContextSize;
    uint32_t               ScratchpadCount;
    uint32_t               CmdEnqueue;
    uint8_t                CmdCycle;
    uint32_t               EvtDequeue;
    uint8_t                EvtCycle;
    uint64_t              *Dcbaa;
    uint64_t               DcbaaPhys;
    XHCI_TRB              *CmdRing;
    uint64_t               CmdRingPhys;
    XHCI_TRB              *EvtRing;
    uint64_t               EvtRingPhys;
    XHCI_ERST_ENTRY       *Erst;
    uint64_t               ErstPhys;
    uint8_t               *InCtx;
    uint64_t               InCtxPhys;
    uint8_t               *ScratchBuf;
    uint64_t               ScratchBufPhys;
    XHCI_HID_SLOT          HidDevices[XHCI_MAX_HID_DEVICES];
    uint32_t               HidCount;
    XHCI_MSC_SLOT          Msc[XHCI_MAX_MSC_DEVICES];
    uint32_t               MscCount;
    uint32_t               HotplugConnects;
    uint32_t               HotplugDisconnects;
    uint32_t               Ready;
} KERNEL_XHCI_DEVICE;

int kernel_xhci_parse_hid_config(const uint8_t *desc, uint32_t len, XHCI_HID_INFO *out);
int kernel_xhci_parse_hid_report_desc(const uint8_t *desc, uint32_t len, XHCI_HID_REPORT_LAYOUT *out);
int kernel_xhci_parse_msc_config(const uint8_t *desc, uint32_t len, XHCI_MSC_INFO *out);
int kernel_xhci_parse_hub_config(const uint8_t *desc, uint32_t len, uint8_t *config_val_out);
void kernel_xhci_decode_hid_kbd(uint8_t prev_report[8], const uint8_t report[8],
    XHCI_KEY_CALLBACK on_key, void *ctx);
void kernel_xhci_decode_hid_mouse(const uint8_t *report, uint32_t len,
    XHCI_MOUSE_CALLBACK on_mouse, void *ctx);
void kernel_xhci_decode_hid_report_pointer(const XHCI_HID_REPORT_LAYOUT *layout,
    const uint8_t *report, uint32_t len, XHCI_MOUSE_CALLBACK on_mouse,
    XHCI_ABS_MOUSE_CALLBACK on_abs_mouse, void *ctx);

int kernel_xhci_init(KERNEL_XHCI_DEVICE *dev, const KERNEL_XHCI_TRANSPORT *transport);
uint32_t kernel_xhci_poll_events(KERNEL_XHCI_DEVICE *dev);
void kernel_xhci_stop(KERNEL_XHCI_DEVICE *dev);

// Shared internal primitives used by msc.c and hub.c
void kernel_xhci_delay_ms(KERNEL_XHCI_DEVICE *dev, uint32_t ms);
void kernel_xhci_ring_doorbell(KERNEL_XHCI_DEVICE *dev, uint32_t slot, uint32_t target);
int kernel_xhci_submit_cmd(KERNEL_XHCI_DEVICE *dev, uint64_t param, uint32_t status,
    uint32_t control, XHCI_TRB *event_out);
int kernel_xhci_ep0_transfer(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id,
    XHCI_TRB *ep0_ring, uint64_t ep0_phys, uint32_t *ep0_enq, uint8_t *ep0_cycle,
    uint64_t setup_pkt, uint64_t data_phys, uint32_t data_len, int dir_in);
int kernel_xhci_wait_transfer(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id, uint32_t ep_dci, uint32_t timeout_ms);
int kernel_xhci_enumerate_device(KERNEL_XHCI_DEVICE *dev, uint32_t root_port_idx,
    uint32_t speed, uint32_t route_string, uint32_t tt_info, uint32_t tier_depth);

// USB 2.0 External Hub (Class 0x09, multi-tier cascade 1..5)
uint32_t kernel_xhci_hub_child_route(uint32_t parent_route, uint32_t tier_depth, uint32_t port);
uint32_t kernel_xhci_hub_child_tt(uint32_t parent_tt, uint32_t hub_slot_id,
    uint32_t hub_speed, uint32_t dev_speed, uint32_t port);
int kernel_xhci_hub_probe(KERNEL_XHCI_DEVICE *dev, uint32_t hub_slot_id, uint32_t root_port_idx,
    uint32_t hub_speed, uint32_t hub_route_string, uint32_t hub_tt_info, uint32_t tier_depth,
    uint8_t config_val, XHCI_TRB *ep0_ring, uint64_t ep0_phys,
    uint32_t *ep0_enq, uint8_t *ep0_cycle);

// USB Mass Storage (Bulk-Only Transport + SCSI)
int kernel_xhci_msc_setup(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id, uint32_t port_idx,
    uint32_t speed, uint32_t route_string, uint32_t tt_info, XHCI_TRB *ep0_ring, uint64_t ep0_phys,
    uint32_t *ep0_enq, uint8_t *ep0_cycle, const XHCI_MSC_INFO *info, uint8_t *dev_ctx, uint64_t dev_ctx_phys);
int kernel_xhci_msc_read_sectors_at(KERNEL_XHCI_DEVICE *dev, uint32_t index, uint64_t lba, uint32_t count, void *buffer);
int kernel_xhci_msc_write_sectors_at(KERNEL_XHCI_DEVICE *dev, uint32_t index, uint64_t lba, uint32_t count, const void *buffer);
int kernel_xhci_msc_read_sectors(KERNEL_XHCI_DEVICE *dev, uint64_t lba, uint32_t count, void *buffer);
int kernel_xhci_msc_write_sectors(KERNEL_XHCI_DEVICE *dev, uint64_t lba, uint32_t count, const void *buffer);

#endif
