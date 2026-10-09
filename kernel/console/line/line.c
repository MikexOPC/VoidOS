#include "line.h"
#include "kernel/input/keyboard/text.h"

// ====== kernel_ps2_line_clear ======

void kernel_ps2_line_clear(KERNEL_PS2_LINE *line)
{
    if (!line) return;
    for (uint32_t index = 0; index <= KERNEL_PS2_LINE_CAPACITY; ++index) line->Data[index] = 0;
    line->Length = 0;
}

// ====== kernel_ps2_line_step ======

uint32_t kernel_ps2_line_step(KERNEL_PS2_LINE *line, uint32_t input)
{
    uint32_t length, start, tail = 0, next, status;
    uint64_t plan, encoded;
    if (!line || line->Length > KERNEL_PS2_LINE_CAPACITY) return KERNEL_PS2_LINE_INVALID;
    length = line->Length;
    if (line->Data[length]) return KERNEL_PS2_LINE_INVALID;
    start = length > 4 ? length - 4 : 0;
    for (uint32_t index = start; index < length; ++index)
        tail |= (uint32_t)(uint8_t)line->Data[index] << ((index - start) * 8);
    plan = kernel_ps2_line_plan(length,tail,input);
    next = (uint32_t)(plan >> 32); status = (uint32_t)plan;
    if (next > KERNEL_PS2_LINE_CAPACITY) return KERNEL_PS2_LINE_INVALID;
    if (status == KERNEL_PS2_LINE_APPEND) {
        encoded = kernel_ps2_utf8(input);
        if (next <= length || next - length > 4 || (uint32_t)(encoded >> 32) != next - length)
            return KERNEL_PS2_LINE_INVALID;
        for (uint32_t index = length; index < next; ++index)
            line->Data[index] = (char)(encoded >> ((index - length) * 8));
        line->Data[next] = 0;
    } else if (status == KERNEL_PS2_LINE_ERASE) {
        if (next > length || length - next > 4) return KERNEL_PS2_LINE_INVALID;
        for (uint32_t index = next; index <= length; ++index) line->Data[index] = 0;
    } else if (status < KERNEL_PS2_LINE_SUBMIT || status > KERNEL_PS2_LINE_FULL || next != length) {
        return KERNEL_PS2_LINE_INVALID;
    }
    line->Length = next;
    return status;
}
