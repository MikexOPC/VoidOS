#ifndef KERNEL_PS2_SHELL_H
#define KERNEL_PS2_SHELL_H

#include "kernel/console/line/line.h"

typedef struct {
    KERNEL_PS2_LINE  Line;
    uint32_t        Truncated;
} KERNEL_PS2_COMMAND;

typedef struct {
    uint32_t            State;
    KERNEL_PS2_COMMAND  Slots[4];
} KERNEL_PS2_COMMAND_QUEUE;

typedef struct {
    uint32_t  Bytes, Keys, Lines;
} KERNEL_PS2_SHELL_STATUS;

#define KERNEL_PS2_QUEUE_OK 0U
#define KERNEL_PS2_QUEUE_EMPTY 1U
#define KERNEL_PS2_QUEUE_FULL 2U
#define KERNEL_PS2_QUEUE_CLOSED 3U
#define KERNEL_PS2_QUEUE_INVALID UINT32_MAX
/* Single producer/consumer, live non-aliasing objects, initialized to zero,
   only these APIs mutate slots/state; no IRQ/SMP access. Pop returns a copy. */
uint32_t kernel_ps2_command_push(KERNEL_PS2_COMMAND_QUEUE *queue, const KERNEL_PS2_COMMAND *command);
uint32_t kernel_ps2_command_pop(KERNEL_PS2_COMMAND_QUEUE *queue, KERNEL_PS2_COMMAND *command);
int kernel_ps2_command_close(KERNEL_PS2_COMMAND_QUEUE *queue);
int kernel_ps2_command_closed_empty(const KERNEL_PS2_COMMAND_QUEUE *queue);
void kernel_ps2_command_clear(KERNEL_PS2_COMMAND *command);
uint64_t kernel_ps2_queue_plan(uint32_t state, uint32_t operation);
uint32_t kernel_ps2_shell_classify(uint64_t word, uint32_t length, uint32_t arguments);
int kernel_ps2_shell_write(const char *data, uint32_t length);
typedef int (*KERNEL_SHELL_OUTPUT)(void *context, const char *data, uint32_t length);
int kernel_ps2_shell_output_bind(KERNEL_SHELL_OUTPUT output, void *context);
int kernel_ps2_shell_output_unbind(KERNEL_SHELL_OUTPUT output, void *context);
/* Return0 output/state failure,1 continue,2 request session stop. */
int kernel_ps2_shell_execute(const KERNEL_PS2_COMMAND *command, const KERNEL_PS2_SHELL_STATUS *status);
#endif
