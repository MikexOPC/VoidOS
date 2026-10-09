#include "shell.h"

// ====== kernel_ps2_command_clear ======

void kernel_ps2_command_clear(KERNEL_PS2_COMMAND *command)
{
    if (!command) return;
    kernel_ps2_line_clear(&command->Line); command->Truncated = 0;
}

// ====== kernel_ps2_command_push ======

uint32_t kernel_ps2_command_push(KERNEL_PS2_COMMAND_QUEUE *queue, const KERNEL_PS2_COMMAND *command)
{
    uint64_t plan;
    uint32_t reply, slot;
    if (!queue || !command || command->Truncated > 1 || command->Line.Length > 256 ||
        command->Line.Data[command->Line.Length]) return KERNEL_PS2_QUEUE_INVALID;
    plan = kernel_ps2_queue_plan(queue->State,1); reply = (uint32_t)plan;
    if ((reply & 255) != KERNEL_PS2_QUEUE_OK) return reply;
    slot = reply >> 8;
    if (slot >= 4) return KERNEL_PS2_QUEUE_INVALID;
    kernel_ps2_command_clear(&queue->Slots[slot]);
    queue->Slots[slot] = *command;
    queue->State = (uint32_t)(plan >> 32);
    return KERNEL_PS2_QUEUE_OK;
}

// ====== kernel_ps2_command_pop ======

uint32_t kernel_ps2_command_pop(KERNEL_PS2_COMMAND_QUEUE *queue, KERNEL_PS2_COMMAND *command)
{
    uint64_t plan;
    uint32_t reply, slot;
    if (!queue || !command) return KERNEL_PS2_QUEUE_INVALID;
    plan = kernel_ps2_queue_plan(queue->State,2); reply = (uint32_t)plan;
    if ((reply & 255) != KERNEL_PS2_QUEUE_OK) return reply;
    slot = reply >> 8;
    if (slot >= 4 || queue->Slots[slot].Line.Length > 256 || queue->Slots[slot].Truncated > 1 ||
        queue->Slots[slot].Line.Data[queue->Slots[slot].Line.Length]) return KERNEL_PS2_QUEUE_INVALID;
    *command = queue->Slots[slot];
    kernel_ps2_command_clear(&queue->Slots[slot]);
    queue->State = (uint32_t)(plan >> 32);
    return KERNEL_PS2_QUEUE_OK;
}

// ====== kernel_ps2_command_close ======

int kernel_ps2_command_close(KERNEL_PS2_COMMAND_QUEUE *queue)
{
    uint64_t plan;
    if (!queue) return 0;
    plan = kernel_ps2_queue_plan(queue->State,3);
    if ((uint32_t)plan) return 0;
    queue->State = (uint32_t)(plan >> 32); return 1;
}

// ====== kernel_ps2_command_closed_empty ======

int kernel_ps2_command_closed_empty(const KERNEL_PS2_COMMAND_QUEUE *queue)
{
    return queue && (uint32_t)kernel_ps2_queue_plan(queue->State,4) == KERNEL_PS2_QUEUE_CLOSED;
}
