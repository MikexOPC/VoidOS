#ifndef OS_X86_PROCESS_SLOT_H
#define OS_X86_PROCESS_SLOT_H
#include "process.h"

#define X86_PROCESS_FRAME_WORDS 84U
#define IPC_MESSAGE_BYTES 128U
#define IPC_WOULD_BLOCK (~5ULL)

typedef struct { BOOT_U64 Storage[142]; } IPC_CHANNEL;
typedef struct {
    _Alignas(16) BOOT_U64 Frame[X86_PROCESS_FRAME_WORDS];
    KERNEL_PHYSICAL_LEASE Lease;
    BOOT_U64 KernelRoot, ImageEntry, WindowEntry;
    BOOT_U64 Entry, RemainingIrqs, Written;
    BOOT_U32 RemainingTicks, Yields, Waiting;
} X86_PROCESS_SLOT;

BOOT_U64 x86_ipc_open(IPC_CHANNEL *channel);
void x86_ipc_close(IPC_CHANNEL *channel);
void x86_ipc_finish(IPC_CHANNEL *channel);
BOOT_U64 x86_ipc_send(IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, const unsigned char *bytes, BOOT_U64 len);
BOOT_U64 x86_ipc_peek(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle, unsigned char *bytes, BOOT_U64 capacity);
BOOT_U64 x86_ipc_consume(IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle);
BOOT_U64 x86_ipc_receive_ready(const IPC_CHANNEL *channel, BOOT_U64 owner, BOOT_U64 handle);

// BSP, IF=0, runtime open, trusted kernel storage. A blocked turn returns 0.
int x86_process_slot_prepare(X86_PROCESS_SLOT *slot, const X86_PROCESS_IMAGE *image);
int x86_process_slot_step(X86_PROCESS_SLOT *slot, X86_PROCESS_RESULT *result,
    IPC_CHANNEL *channel, BOOT_U64 endpoint, int renew_budget);
int x86_process_slot_reap(X86_PROCESS_SLOT *slot);
#endif
