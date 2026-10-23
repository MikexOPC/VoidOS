#ifndef TEST_VTD_DMA_EDU_H
#define TEST_VTD_DMA_EDU_H
#include "arch/x86/boot/uefi/handoff/protocol/start_info.h"

// QEMU v11.1.0 hw/misc/edu.c: test-only PCI device, never part of a production image.
#define EDU_ID           0x11e81234U
#define EDU_IDENTITY     0x010000edU
#define EDU_BUFFER       0x40000ULL
#define EDU_BUFFER_BYTES 4096U
#define EDU_TO_DEVICE    0U
#define EDU_TO_RAM       1U

// Register access supplied by the caller: an owned UC borrow in the kernel, a model on the host.
typedef struct {
    void  *State;
    int  (*Read)(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 *value);
    int  (*Write)(void *state, BOOT_U32 offset, BOOT_U32 width, BOOT_U64 value);
    BOOT_U64  (*Ticks)(void *state);
    BOOT_U64  Hz, Mask;
} EDU_BUS;

int edu_identify(const EDU_BUS *bus);
int edu_transfer(const EDU_BUS *bus, BOOT_U64 source, BOOT_U64 destination, BOOT_U32 bytes, BOOT_U32 direction);
#endif
