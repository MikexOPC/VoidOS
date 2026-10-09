#include "shell.h"
#include "kernel/diagnostics/log.h"

// ====== kernel_ps2_shell_write ======

int kernel_ps2_shell_write(const char *data, uint32_t length)
{
    char chunk[129];
    if (!data || length > 256 || data[length]) return 0;
    for (uint32_t offset = 0; offset < length;) {
        uint32_t count = length - offset;
        if (count > 128) count = 128;
        for (uint32_t index = 0; index < count; ++index) chunk[index] = data[offset + index];
        chunk[count] = 0;
        if (!kernel_text(chunk)) return 0;
        offset += count;
    }
    return 1;
}

// ====== kernel_ps2_shell_execute ======

int kernel_ps2_shell_execute(const KERNEL_PS2_COMMAND *command, const KERNEL_PS2_SHELL_STATUS *status)
{
    uint32_t start = 0, end, argument, kind;
    uint64_t word = 0;
    const KERNEL_PS2_LINE *line;
    if (!command || !status || command->Truncated > 1 || command->Line.Length > 256) return 0;
    line = &command->Line;
    if (line->Data[line->Length]) return 0;
    for (uint32_t index = 0; index < line->Length; ++index)
        if ((uint8_t)line->Data[index] < 32 || (uint8_t)line->Data[index] == 127) return 0;
    if (command->Truncated) return kernel_log(2,"SHELL_LINE_REJECTED_TRUNCATED");
    while (start < line->Length && line->Data[start] == ' ') ++start;
    end = start;
    while (end < line->Length && line->Data[end] != ' ') ++end;
    for (uint32_t index = 0; index < end - start && index < 8; ++index)
        word |= (uint64_t)(uint8_t)line->Data[start + index] << (index * 8);
    argument = end;
    while (argument < line->Length && line->Data[argument] == ' ') ++argument;
    kind = kernel_ps2_shell_classify(word,end - start,argument != line->Length);
    if (kind == UINT32_MAX) return 0;
    if (!kind) return kernel_log(1,"SHELL_EMPTY");
    if (kind == 1) return kernel_log(1,"SHELL_HELP: help | echo <testo> | status | exit");
    if (kind == 2) return kernel_text("SHELL_ECHO: ") &&
        kernel_ps2_shell_write(line->Data + argument,line->Length - argument) && kernel_text("\n");
    if (kind == 3) return kernel_log(1,"SHELL_STATUS: BSP PS2, cooperative, kernel tasks") &&
        kernel_text("SHELL_INPUT_BYTES: ") && kernel_number(status->Bytes) &&
        kernel_text("\nSHELL_INPUT_KEYS: ") && kernel_number(status->Keys) &&
        kernel_text("\nSHELL_INPUT_LINES: ") && kernel_number(status->Lines) && kernel_text("\n");
    if (kind == 4) return kernel_log(1,"SHELL_EXIT_REQUEST") ? 2 : 0;
    return kernel_log(2,kind == 6 ? "SHELL_BAD_ARGUMENTS" : "SHELL_UNKNOWN_COMMAND");
}
