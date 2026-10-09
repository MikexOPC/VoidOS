#ifndef KERNEL_PS2_CONTROLLER_H
#define KERNEL_PS2_CONTROLLER_H
#include <stdint.h>
#include "kernel/firmware/acpi/root.h"

#define KERNEL_PS2_PREPARED 1U
#define KERNEL_PS2_DIRTY    2U
#define KERNEL_PS2_CLOSED   3U
#define KERNEL_PS2_IO_ERROR 1U
#define KERNEL_PS2_TIMEOUT  2U
#define KERNEL_PS2_PROTOCOL 3U
#define KERNEL_PS2_CLEANUP_FAILED 0x80000000U

typedef struct {
    void     *Context;
    int      (*Read)(void *context, uint16_t port, uint8_t *value);
    int      (*Write)(void *context, uint16_t port, uint8_t value);
    int      (*Clock)(void *context, uint32_t *ticks);
    uint32_t Mask;
    uint32_t Timeout;
} KERNEL_PS2_IO;

typedef struct {
    KERNEL_PS2_IO Io;
    uint32_t      Phase;
    uint32_t      Error;
    uint32_t      Mode;
    uint32_t      Reserved;
} KERNEL_PS2_CONTROLLER;

typedef struct {
    uint64_t Physical;
    uint32_t Length;
    uint32_t Present;
} KERNEL_PS2_DESCRIPTION;

/*
    Discovery writes output only on success. Present=0 is supported absence.
    No port access before Present=1, validated clock and exclusive BSP/IF=0
    ownership. Callbacks and Context must outlive owner; they must not reenter.
    Prepare leaves both interfaces and IRQs disabled, translation off,
    keyboard scanning disabled, set 2 queried. Close retries dirty cleanup.
    Never restore the firmware's IRQ/scanning policy without a kernel owner.
*/

int kernel_ps2_discover(const KERNEL_ACPI_DIRECTORY *directory, uint32_t bits,
    KERNEL_ACPI_READ read, void *context, KERNEL_PS2_DESCRIPTION *output);
int kernel_ps2_prepare(KERNEL_PS2_CONTROLLER *owner, const KERNEL_PS2_IO *io);
int kernel_ps2_close(KERNEL_PS2_CONTROLLER *owner);
int kernel_ps2_start(KERNEL_PS2_CONTROLLER *owner);
int kernel_ps2_stop(KERNEL_PS2_CONTROLLER *owner);

/* Internal protocol operations; only an initialized exclusive owner. */
int kernel_ps2_send(KERNEL_PS2_CONTROLLER *owner, uint16_t port, uint8_t byte);
int kernel_ps2_receive(KERNEL_PS2_CONTROLLER *owner, uint8_t *byte);
int kernel_ps2_drain(KERNEL_PS2_CONTROLLER *owner);
int kernel_ps2_mode(KERNEL_PS2_CONTROLLER *owner, uint8_t mode);
int kernel_ps2_command(KERNEL_PS2_CONTROLLER *owner, uint8_t command);
#endif
