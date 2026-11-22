#include "shell.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_USER_PROCESS
int kernel_shell_bridge_status(void);
int kernel_shell_proc_run(uint64_t result[5]);
#endif
static KERNEL_SHELL_OUTPUT output;
static void *output_context;
static int exit_requested;
#define NUMBER(literal, value) number_private(literal, (uint32_t)(sizeof(literal) - 1U), value)
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

// ====== log_private ======

static int log_private(uint32_t level, const char *data, uint32_t length)
{
    return data && length < 256 && kernel_log(level, data) && video_private(data, length) && video_private("\n", 1);
}

// ====== number_private ======

static int number_private(const char *label, uint32_t length, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[64];
    if (length > sizeof(text) - 20) return 0;
    for (uint32_t index = 0; index < length; ++index) text[index] = label[index];
    text[length] = '0';
    text[length + 1] = 'x';
    for (uint32_t index = 0; index < 16; ++index)
        text[length + index + 2] = digits[(value >> (60 - 4 * index)) & 15];
    text[length + 18] = '\n';
    text[length + 19] = 0;
    return kernel_text(text) && video_private(text,length + 19);
}

// ====== serial_private ======

static int serial_private(const char *data, uint32_t length) {
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

// ====== kernel_ps2_shell_write ======

int kernel_ps2_shell_write(const char *data, uint32_t length) {
    return serial_private(data,length) && video_private(data,length);
}

// ====== echo_private ======

static int echo_private(const char *data, uint32_t length) {
    static const char prefix[] = "SHELL_ECHO: ";
    return kernel_text(prefix) && serial_private(data,length) && kernel_text("\n") &&
        video_private(prefix,sizeof(prefix) - 1) && video_private(data,length) && video_private("\n",1);
}

// ====== decimal_private ======

static uint64_t decimal_private(const char *text, uint32_t length) {
    uint64_t low = 0, high = 0;
    if (length > 9)
        return UINT64_MAX;
    for (uint32_t index = 0; index < length; ++index) {
        if (index < 8)
            low |= (uint64_t)(uint8_t)text[index] << (index * 8);
        else
            high = (uint8_t)text[index];
    }
    return kernel_ps2_shell_number(low,high,length);
}

// ====== remote_private ======

static int remote_private(const char *text, uint32_t length)
{
    uint64_t value, cpu = 1, tasks = 1, result[8] = {0};
    int automatic = 0;
    uint32_t first = 0;
    while (first < length && text[first] != ' ')
        ++first;
    value = decimal_private(text,first);
    if (first < length) {
        uint32_t start = first + 1, second = start;
        while (second < length && text[second] != ' ')
            ++second;
        automatic = second - start == 4 && text[start] == 'a' && text[start + 1] == 'u' &&
            text[start + 2] == 't' && text[start + 3] == 'o';
        if (!automatic)
            cpu = decimal_private(text + start,second - start);
        if (second - start == 1 && text[start] == '0')
            cpu = 0;
        if (second < length) {
            uint32_t third = second + 1;
            tasks = decimal_private(text + third,length - third);
        }
    }
    if (value == UINT64_MAX ||
        cpu >= 64 ||
        !tasks ||
        tasks == UINT64_MAX ||
        tasks > 32
    )
        return LOG(2,"SHELL_BAD_ARGUMENTS");
#ifdef KERNEL_AP_TASKS
    uint32_t target = automatic ? KERNEL_SHELL_CPU_ANY : (uint32_t)cpu;
    if (tasks > 1) {
        if (!kernel_shell_remote_batch(target,value,(uint32_t)tasks,result))
            return LOG(2,"SHELL_AP_FAILED");
        if (!NUMBER("SHELL_AP_TASKS: ",result[6]) || !NUMBER("SHELL_AP_MAX_INDEX: ",result[7]))
            return 0;
    } else if (!kernel_shell_remote_run(target,value,result)) {
        return LOG(2,"SHELL_AP_FAILED");
    }
    return NUMBER("SHELL_AP_CPUS: ",result[4]) && NUMBER("SHELL_AP_MIGRATIONS: ",result[5]) &&
        NUMBER("SHELL_AP_CPU: ",result[0]) && NUMBER("SHELL_AP_COUNT: ",value) &&
        NUMBER("SHELL_AP_SUM: ",result[1]) && NUMBER("SHELL_AP_CHECKSUM: ",result[2]) &&
        NUMBER("SHELL_AP_WAITS: ",result[3]);
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
    if (kind == 1) return LOG(1,"SHELL_HELP: help | echo <testo> | status | bridge status | proc | ap <n> [cpu|auto] | exit");
    if (kind == 2) return echo_private(line->Data + argument,line->Length - argument);
    if (kind == 3) return LOG(1,"SHELL_STATUS: BSP PS2, kernel tasks") &&
        NUMBER("SHELL_INPUT_BYTES: ",status->Bytes) && NUMBER("SHELL_INPUT_KEYS: ",status->Keys) &&
        NUMBER("SHELL_INPUT_LINES: ",status->Lines);
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
    if (kind == 9) {
#ifdef KERNEL_USER_PROCESS
        uint64_t proc_result[5] = {0};
        if (!kernel_shell_proc_run(proc_result))
            return LOG(2,"SHELL_PROC_FAILED");
        return NUMBER("SHELL_PROC_SPAWNED: ",proc_result[0]) &&
            NUMBER("SHELL_PROC_PEAK_ACTIVE: ",proc_result[1]) &&
            NUMBER("SHELL_PROC_IPC_OK: ",proc_result[2]) &&
            NUMBER("SHELL_PROC_FAULT_VECTOR: ",proc_result[3]) &&
            NUMBER("SHELL_PROC_REAPED: ",proc_result[4]) &&
            LOG(0,"SHELL_PROC_OK");
#else
        return LOG(2,"SHELL_PROC_UNAVAILABLE");
#endif
    }
    return kind == 6 ? LOG(2,"SHELL_BAD_ARGUMENTS") : LOG(2,"SHELL_UNKNOWN_COMMAND");
}
