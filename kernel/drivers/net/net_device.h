#ifndef OS_KERNEL_DRIVERS_NET_NET_DEVICE_H
#define OS_KERNEL_DRIVERS_NET_NET_DEVICE_H

#include <stddef.h>
#include <stdint.h>

// ====== net_device_contract ======

#define KERNEL_NET_OK               0
#define KERNEL_NET_ERR_EMPTY        1
#define KERNEL_NET_ERR_PARAM        (-1)
#define KERNEL_NET_ERR_IO           (-2)
#define KERNEL_NET_ERR_TIMEOUT      (-3)
#define KERNEL_NET_ERR_FULL         (-4)
#define KERNEL_NET_ERR_UNSUPPORTED  (-5)

#define KERNEL_NET_ETH_HEADER_BYTES 14U
#define KERNEL_NET_MTU              1500U
#define KERNEL_NET_MAX_FRAME_BYTES  1518U

typedef struct KERNEL_NET_DEVICE KERNEL_NET_DEVICE;

typedef struct {
    int      (*SendFrame)(void *context, const uint8_t *frame, uint32_t length);
    int      (*PollRx)(void *context, uint8_t *frame_out, uint32_t capacity, uint32_t *length_out);
    int      (*LinkUp)(void *context);
    void     (*WaitEvent)(void *context, uint32_t timeout_ms);
} KERNEL_NET_OPS;

struct KERNEL_NET_DEVICE {
    const KERNEL_NET_OPS  *Ops;
    void                  *Context;
    uint8_t                Mac[6];
    uint16_t               Mtu;
    uint32_t               LinkUp;
    uint32_t               Reserved;
    uint64_t               TxPackets;
    uint64_t               RxPackets;
    uint64_t               TxBytes;
    uint64_t               RxBytes;
    uint64_t               RxDrops;
};

#endif
