#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/shell/shell.h"
#include "kernel/diagnostics/log.h"
static unsigned checks, calls, fail_at, used;
static char output[16384];
#define CHECK(e) do { ++checks; assert(e); } while (0)

// ====== bounded_log_mocks ======

int kernel_text(const char *text)
{
    size_t length = strlen(text);
    if (++calls == fail_at) return 0;
    if (length >= 256 || length > sizeof(output) - 1 - used) return 0;
    memcpy(output + used,text,length + 1); used += (unsigned)length;
    return 1;
}
int kernel_log(unsigned level, const char *text)
{
    (void)level; return kernel_text(text) && kernel_text("\n");
}

// ====== command_fixture ======

static char video[1024];
static uint32_t video_used, video_fail;
static uint32_t video_interleave;

// ====== video_output ======

static int video_output(void *context, const char *data, uint32_t length)
{
    CHECK(context == video && length <= 256);
    if (video_interleave) {
        CHECK(used && output[used - 1] == '\n');
        CHECK(kernel_log(1,"OTHER_TASK"));
    }
    if (video_fail || length > sizeof(video) - video_used - 1) return 0;
    memcpy(video + video_used, data, length); video_used += length;
    video[video_used] = 0;
    return 1;
}

// ====== command_fixture ======

static void fixture(KERNEL_PS2_COMMAND *command, const char *text)
{
    kernel_ps2_command_clear(command);
    command->Line.Length = (uint32_t)strlen(text);
    CHECK(command->Line.Length <= 256);
    memcpy(command->Line.Data,text,command->Line.Length + 1);
    calls = fail_at = used = 0; output[0] = 0;
}

// ====== shell_queue_and_dispatch ======

int main(void)
{
    struct {
        uint64_t                  Before;
        KERNEL_PS2_COMMAND_QUEUE  Queue;
        uint64_t                  After;
    } guarded = {0};
    KERNEL_PS2_COMMAND command = {0}, popped = {0};
    KERNEL_PS2_SHELL_STATUS status = {1,2,3};
    guarded.Before = 0x123456789abcdef0ULL; guarded.After = ~guarded.Before;
    for (unsigned repetition = 0; repetition < 10000; ++repetition) {
        for (unsigned slot = 0; slot < 4; ++slot) {
            fixture(&command,"echo x"); command.Line.Data[5] = (char)('a' + slot);
            CHECK(kernel_ps2_command_push(&guarded.Queue,&command) == KERNEL_PS2_QUEUE_OK);
        }
        CHECK(kernel_ps2_command_push(&guarded.Queue,&command) == KERNEL_PS2_QUEUE_FULL);
        for (unsigned slot = 0; slot < 4; ++slot) {
            CHECK(kernel_ps2_command_pop(&guarded.Queue,&popped) == KERNEL_PS2_QUEUE_OK);
            CHECK(popped.Line.Data[5] == (char)('a' + slot));
            CHECK(!guarded.Queue.Slots[slot].Line.Length && !guarded.Queue.Slots[slot].Line.Data[5]);
        }
        CHECK(kernel_ps2_command_pop(&guarded.Queue,&popped) == KERNEL_PS2_QUEUE_EMPTY);
    }
    fixture(&command,"help");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_HELP"));
    fixture(&command,"help x");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_BAD_ARGUMENTS"));
    fixture(&command,"status");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_STATUS"));
    const char *bad_ap[] = {"ap 0", "ap 100000001", "ap 1 64", "ap 1 -1", "ap 1 2 3", "ap 1 auto x"};
    for (uint32_t index = 0; index < sizeof(bad_ap) / sizeof(bad_ap[0]); ++index) {
        fixture(&command,bad_ap[index]);
        CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_BAD_ARGUMENTS"));
    }
    fixture(&command,"ap 1 63");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_AP_UNAVAILABLE"));
    fixture(&command,"ap 100000000 63");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_AP_UNAVAILABLE"));
    fixture(&command,"ap 1 0");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_AP_UNAVAILABLE"));
    fixture(&command,"ap 1 auto");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_AP_UNAVAILABLE"));
    fixture(&command,"exit"); CHECK(kernel_ps2_shell_execute(&command,&status) == 2);
    fixture(&command,"echo exit");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_ECHO: exit"));
    fixture(&command,"  echo \xc3\xa8 \xe2\x82\xac");
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"\xc3\xa8 \xe2\x82\xac"));
    fixture(&command,"echo"); CHECK(kernel_ps2_shell_execute(&command,&status) == 1);
    fixture(&command,"nope"); CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"SHELL_UNKNOWN"));
    fixture(&command,""); CHECK(kernel_ps2_shell_execute(&command,&status) == 1);
    fixture(&command,"exit"); command.Truncated = 1;
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strstr(output,"REJECTED_TRUNCATED"));
    fixture(&command,"echo x"); command.Line.Data[5] = 27;
    CHECK(!kernel_ps2_shell_execute(&command,&status));
    fixture(&command,"echo x");
    for (unsigned point = 1; point <= 3; ++point) {
        used = calls = 0; fail_at = point;
        CHECK(!kernel_ps2_shell_execute(&command,&status));
        CHECK(command.Line.Length == 6 && command.Line.Data[5] == 'x');
    }
    fixture(&command,""); command.Line.Length = 256;
    memcpy(command.Line.Data,"echo ",5); memset(command.Line.Data + 5,'a',251);
    CHECK(kernel_ps2_shell_execute(&command,&status) == 1 && strlen(output) == 264);
    CHECK(kernel_ps2_command_push(&guarded.Queue,&command) == KERNEL_PS2_QUEUE_OK);
    CHECK(kernel_ps2_command_close(&guarded.Queue));
    CHECK(kernel_ps2_command_push(&guarded.Queue,&command) == KERNEL_PS2_QUEUE_CLOSED);
    CHECK(kernel_ps2_command_pop(&guarded.Queue,&popped) == KERNEL_PS2_QUEUE_OK && popped.Line.Length == 256);
    CHECK(kernel_ps2_command_closed_empty(&guarded.Queue));
    CHECK(kernel_ps2_command_pop(&guarded.Queue,&popped) == KERNEL_PS2_QUEUE_CLOSED);
    CHECK(guarded.Before == 0x123456789abcdef0ULL && guarded.After == ~guarded.Before);
    CHECK(!kernel_ps2_shell_output_bind(NULL, video));
    CHECK(kernel_ps2_shell_output_bind(video_output, video));
    CHECK(!kernel_ps2_shell_output_bind(video_output, video));
    CHECK(!kernel_ps2_shell_output_unbind(video_output, &command));
    video_interleave = 1;
    fixture(&command,"echo \xc3\xa8 \xe2\x82\xac");
    CHECK(kernel_ps2_shell_execute(&command, &status) == 1);
    CHECK(!strcmp(video, "SHELL_ECHO: \xc3\xa8 \xe2\x82\xac\n"));
    video_used = 0; fixture(&command,"status");
    CHECK(kernel_ps2_shell_execute(&command, &status) == 1);
    CHECK(strstr(video, "SHELL_INPUT_BYTES: 0x0000000000000001"));
    CHECK(strstr(output, "SHELL_INPUT_BYTES: 0x0000000000000001\n"));
    CHECK(strstr(output, "SHELL_INPUT_KEYS: 0x0000000000000002\n"));
    CHECK(strstr(output, "SHELL_INPUT_LINES: 0x0000000000000003\n"));
    video_fail = 1; fixture(&command,"exit");
    CHECK(kernel_ps2_shell_execute(&command, &status) == 0);
    CHECK(kernel_ps2_shell_output_unbind(video_output, video));
    CHECK(!kernel_ps2_shell_output_unbind(video_output, video));
    CHECK(kernel_ps2_shell_execute(&command, &status) == 2);
    printf("PASS: shell C/Rust %u checks, FIFO, close, output failure and bounds\n",checks);
    return 0;
}
