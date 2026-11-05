#ifndef OS_KERNEL_SCHEDULER_LOT_H
#define OS_KERNEL_SCHEDULER_LOT_H
#include "scheduler.h"

#define KERNEL_LOT_OPEN 0U
#define KERNEL_LOT_ENTER 1U
#define KERNEL_LOT_COMPLETE 2U
#define KERNEL_LOT_QUIESCE 3U
#define KERNEL_LOT_QUERY 4U
#define KERNEL_LOT_FAIL 5U
#define KERNEL_LOT_CLOSE 6U

typedef struct {
    uint32_t  status;
    uint32_t  failed;
    uint64_t  group;
    uint64_t  token;
    uint64_t  participants;
    uint64_t  entered;
    uint64_t  completed;
    uint64_t  quiesced;
} KERNEL_LOT_REPLY;
_Static_assert(sizeof(KERNEL_LOT_REPLY) == 56, "Lot reply size");
_Static_assert(_Alignof(KERNEL_LOT_REPLY) == 8, "Lot reply alignment");
_Static_assert(offsetof(KERNEL_LOT_REPLY, group) == 8, "Lot group offset");
_Static_assert(offsetof(KERNEL_LOT_REPLY, quiesced) == 48, "Lot quiescence offset");

// ====== kernel_scheduler_lot_on ======

KERNEL_LOT_REPLY kernel_scheduler_lot_on(uint32_t cpu, uint32_t operation,
    uint64_t group, uint64_t token, uint64_t participants);
#endif
