#include "shell.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_USER_PROCESS
int kernel_shell_bridge_status(void);
#endif
static KERNEL_SHELL_OUTPUT output;
static void *output_context;
static int exit_requested;
#define TEXT(literal) text_private(literal, (uint32_t)(sizeof(literal) - 1U))
#define LOG(level, literal) log_private(level, literal, (uint32_t)(sizeof(literal) - 1U))

// ====== kernel_shell_exit_requested ======

int kernel_shell_exit_requested(void)
{
    return exit_requested;
}

// ====== kernel_ps2_shell_output_bind ======

int kernel_ps2_shell_output_bind(KERNEL_SHELL_OUTPUT callback, void *context)
{
    if (!callback || !context || output || output_context) return 0;
    output = callback; output_context = context;
    return 1;
}

// ====== kernel_ps2_shell_output_unbind ======

int kernel_ps2_shell_output_unbind(KERNEL_SHELL_OUTPUT callback, void *context)
{
    if (!callback || callback != output || context != output_context) return 0;
    output = 0; output_context = 0;
    return 1;
}

// ====== video_private ======

static int video_private(const char *data, uint32_t length)
{
    return !output || output(output_context, data, length);
}

// ====== text_private ======

static int text_private(const char *data, uint32_t length)
{
    return data && length < 256 && kernel_text(data) && video_private(data, length);
}

// ====== log_private ======

static int log_private(uint32_t level, const char *data, uint32_t length)
{
    return data && length < 256 && kernel_log(level, data) && video_private(data, length) && video_private("\n", 1);
}

// ====== number_private ======

static int number_private(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[18] = {'0', 'x'};
    for (uint32_t index = 0; index < 16; ++index)
        text[index + 2] = digits[(value >> (60 - 4 * index)) & 15];
    return kernel_number(value) && video_private(text, sizeof(text));
}

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
    return video_private(data, length);
}

// ====== remote_private ======

static int remote_private(const char *text, uint32_t length)
{
    uint64_t word = 0, value, result[4] = {0};
    for (uint32_t index = 0; index < length && index < 8; ++index)
        word |= (uint64_t)(uint8_t)text[index] << (index * 8);
    value = length <= 8 ? kernel_ps2_shell_count(word,length) : UINT64_MAX;
    if (value == UINT64_MAX) return LOG(2,"SHELL_BAD_ARGUMENTS");
#ifdef KERNEL_AP_TASKS
    if (!kernel_shell_remote(value,result)) return LOG(2,"SHELL_AP_FAILED");
    return TEXT("SHELL_AP_CPU: ") && number_private(result[0]) &&
        TEXT("\nSHELL_AP_COUNT: ") && number_private(value) &&
        TEXT("\nSHELL_AP_SUM: ") && number_private(result[1]) &&
        TEXT("\nSHELL_AP_CHECKSUM: ") && number_private(result[2]) &&
        TEXT("\nSHELL_AP_WAITS: ") && number_private(result[3]) && TEXT("\n");
#else
    (void)result;
    return LOG(2,"SHELL_AP_UNAVAILABLE");
#endif
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
    if (command->Truncated) return LOG(2,"SHELL_LINE_REJECTED_TRUNCATED");
    while (start < line->Length && line->Data[start] == ' ') ++start;
    end = start;
    while (end < line->Length && line->Data[end] != ' ') ++end;
    for (uint32_t index = 0; index < end - start && index < 8; ++index)
        word |= (uint64_t)(uint8_t)line->Data[start + index] << (index * 8);
    argument = end;
    while (argument < line->Length && line->Data[argument] == ' ') ++argument;
    kind = kernel_ps2_shell_classify(word,end - start,argument != line->Length);
    if (kind == UINT32_MAX) return 0;
    if (!kind) return LOG(1,"SHELL_EMPTY");
    if (kind == 1) return LOG(1,"SHELL_HELP: help | echo <testo> | status | bridge status | ap <n> | exit");
    if (kind == 2) return TEXT("SHELL_ECHO: ") &&
        kernel_ps2_shell_write(line->Data + argument,line->Length - argument) && TEXT("\n");
    if (kind == 3) return LOG(1,"SHELL_STATUS: BSP PS2, kernel tasks") &&
        TEXT("SHELL_INPUT_BYTES: ") && number_private(status->Bytes) &&
        TEXT("\nSHELL_INPUT_KEYS: ") && number_private(status->Keys) &&
        TEXT("\nSHELL_INPUT_LINES: ") && number_private(status->Lines) && TEXT("\n");
    if (kind == 4) { exit_requested = 1; return LOG(1,"SHELL_EXIT_REQUEST") ? 2 : 0; }
    if (kind == 7) return remote_private(line->Data + argument,line->Length - argument);
    if (kind == 8) {
        uint64_t subcommand = 0;
        for (uint32_t index = argument; index < line->Length && index - argument < 8; ++index)
            subcommand |= (uint64_t)(uint8_t)line->Data[index] << ((index - argument) * 8);
        if (kernel_ps2_shell_classify(subcommand,line->Length - argument,0) != 3)
            return LOG(2,"SHELL_BAD_ARGUMENTS");
#ifdef KERNEL_USER_PROCESS
        return kernel_shell_bridge_status() ? LOG(0,"SHELL_BRIDGE_STATUS_OK") : LOG(2,"SHELL_BRIDGE_UNAVAILABLE");
#else
        return LOG(2,"SHELL_BRIDGE_UNAVAILABLE");
#endif
    }
    return kind == 6 ? LOG(2,"SHELL_BAD_ARGUMENTS") : LOG(2,"SHELL_UNKNOWN_COMMAND");
}
