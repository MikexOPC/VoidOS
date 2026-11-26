#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/scheduler/payload.h"
#ifdef KERNEL_USER_PROCESS
#include "kernel/gui/gui.h"
static KERNEL_GUI gui;
static KERNEL_INPUT_EVENT gui_events[32];
static uint32_t gui_count, serial_calls;

// ====== gui_output_boundary ======

KERNEL_GUI *kernel_gui_global(void) { return &gui; }
uint64_t kernel_gui_display_owner(KERNEL_GUI *value) { assert(value == &gui); return 20; }
int kernel_gui_input_push_key(KERNEL_GUI *value, uint32_t code, uint32_t scalar, uint32_t flags)
{
    assert(value == &gui && gui_count < 32);
    gui_events[gui_count++] = (KERNEL_INPUT_EVENT){KERNEL_INPUT_KEY, flags, code, scalar, 0, 0};
    return 1;
}
#endif

static X86_PS2_SESSION owner;
static uint64_t queue[259];
static uint32_t head, count, submitted, closed, waits, recovery;
static uint32_t cancel_profile, cancelled, expected_modifiers;
static jmp_buf failure;
static int expect_failure;
static uint64_t interrupt_flag;
#ifdef KERNEL_CONSOLE_SHELL
static uint32_t edits;

// ====== video_edit_boundary ======

int x86_console_edit(X86_PS2_SESSION *session)
{
    assert(session == &owner);
    if (!edits) assert(!owner.Line.Length && !owner.Decoder &&
        owner.TextState == (cancel_profile ? expected_modifiers : 0));
    ++edits;
    return 1;
}
#endif

// ====== simulated_preempt_section ======

uint64_t kernel_preempt_section_enter(void)
{
    uint64_t saved_if = interrupt_flag;
    interrupt_flag = 0;
    return saved_if;
}

void kernel_preempt_section_leave(uint64_t saved_if)
{
    assert(!interrupt_flag && saved_if == 0x200);
    interrupt_flag = saved_if;
}

// ====== scheduler_and_output_boundaries ======

X86_PS2_SESSION *x86_ps2_session(void) { return &owner; }
uint64_t x86_scheduler_generation(void) { return 7; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void)
{
    if (expect_failure) longjmp(failure,1);
    abort();
}
int kernel_text(const char *text) {
    assert(text);
#ifdef KERNEL_USER_PROCESS
    ++serial_calls;
#endif
    return 1;
}
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned level, const char *text)
{
    (void)level; assert(text);
    if (!strcmp(text,"PS2_EDIT_CANCEL")) ++cancelled;
    return 1;
}
int x86_ps2_shell_submit(void)
{
    assert(owner.Line.Length == 1 && owner.Line.Data[0] == 'a' && !owner.LineTruncated);
    ++submitted;
    return 1;
}
void x86_ps2_shell_close(void) { ++closed; }
void x86_scheduler_yield(void) { }
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t source, uint64_t token,
    uint32_t consumer, uint64_t generation)
{
    assert(!interrupt_flag);
    assert(event == 4 && source == (uint64_t)&owner && token == 3 && !consumer && generation == 7);
    return head < count ? queue[head++] : KERNEL_PAYLOAD_EMPTY;
}
KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
    assert(interrupt_flag == 0x200 && id == 4);
    reply.epoch = 1;
    if (reason == X86_TASK_WAIT) {
        assert(token == 1);
        ++waits;
        if (cancel_profile && waits == 1) {
            assert(!owner.Stop && !owner.Failed && !owner.Line.Length && !owner.LineTruncated);
            assert(owner.TextState == expected_modifiers && !owner.Resyncs && !submitted && cancelled == 2);
            if (!expected_modifiers) {
                queue[count++] = KERNEL_PAYLOAD_DATA | 0x1c;
                queue[count++] = KERNEL_PAYLOAD_DATA | 0x5a;
            } else owner.Stop = owner.Quiet = 1;
        } else if (recovery && waits == 1) {
            assert(!owner.Decoder && !owner.TextState && !owner.Line.Length && !owner.LineTruncated);
            assert(!submitted && owner.Resyncs == 1 && !owner.Failed && !owner.Stop);
            queue[count++] = KERNEL_PAYLOAD_DATA | 0x1c;
            queue[count++] = KERNEL_PAYLOAD_DATA | 0x5a;
        } else owner.Stop = owner.Quiet = 1;
    } else assert(reason == X86_TASK_OBSERVE);
    return reply;
}

// ====== interrupted_stream_fixture ======

static void reset_private(void)
{
    memset(&owner,0,sizeof(owner));
    memset(queue,0,sizeof(queue));
    head = count = submitted = closed = waits = recovery = 0;
    cancel_profile = cancelled = expected_modifiers = 0;
#ifdef KERNEL_CONSOLE_SHELL
    edits = 0;
#endif
    interrupt_flag = 0x200;
    owner.Binding = 3;
    owner.Decoder = 1;
    owner.TextState = 9;
    owner.Line.Data[0] = 'x'; owner.Line.Length = 1;
    owner.LineTruncated = 1;
    owner.DeviceErrors = 1;
}

// ====== queued_enter_and_prefix_do_not_survive_fault ======

int main(void)
{
#ifdef KERNEL_USER_PROCESS
    reset_private();
    owner.DeviceErrors = owner.Decoder = owner.TextState = 0;
    owner.Line.Length = 0;
    const unsigned char bytes[] = {0x12, 0x1c, 0xf0, 0x1c, 0xf0, 0x12, 0x1c, 0x1c, 0xf0, 0x1c, 0x76};
    for (uint32_t index = 0; index < sizeof(bytes); ++index)
        queue[count++] = KERNEL_PAYLOAD_DATA | bytes[index];
    x86_ps2_consume();
    assert(gui_count == 8 && !submitted && !cancelled && !serial_calls);
    assert(gui_events[0].Code == 0x10012 && gui_events[0].Value == 0);
    assert(gui_events[1].Value == 'A');
    assert(gui_events[2].Code == 0x2001c && gui_events[2].Value == 0);
    assert(gui_events[3].Code == 0x20012 && gui_events[3].Value == 0);
    assert(gui_events[4].Value == 'a' && gui_events[5].Value == 'a');
    assert(gui_events[6].Code == 0x2001c && gui_events[6].Value == 0);
    assert(gui_events[7].Code == 0x10076 && gui_events[7].Value == 0);
    reset_private();
    gui_count = 0;
    x86_ps2_consume();
    assert(gui_count == 1 && gui_events[0].Flags == KERNEL_INPUT_FLAG_RESYNC);
    puts("PASS GUI keyboard: real decoder/text/consumer; GUI queue and scheduler mocked");
    return 0;
#else
    for (uint32_t bytes = 0; bytes <= 256; ++bytes) {
        reset_private();
        for (uint32_t index = 0; index < bytes; ++index)
            queue[count++] = KERNEL_PAYLOAD_DATA | (index & 1 ? 0xe0 : 0x5a);
        queue[count++] = KERNEL_PAYLOAD_LOSS | (UINT64_C(17) << 32);
        recovery = 1;
        x86_ps2_consume();
        assert(submitted == 1 && closed == 1 && owner.ConsumerDone);
        assert(owner.Keys == 2 && owner.Resyncs == 1 && !owner.Failed && head == count);
    }
    reset_private();
    x86_ps2_consume();
    assert(!submitted && closed == 1 && owner.Resyncs == 1 && owner.ConsumerDone);
    for (uint32_t position = 0; position < 2; ++position) {
        for (uint32_t kind = 0; kind < 2; ++kind) {
            reset_private();
            if (position) queue[count++] = KERNEL_PAYLOAD_DATA | 0x5a;
            queue[count++] = kind ? KERNEL_PAYLOAD_BUSY : KERNEL_PAYLOAD_DENIED;
            expect_failure = 1;
            if (!setjmp(failure)) { x86_ps2_consume(); abort(); }
            expect_failure = 0;
            assert(!submitted && !closed);
        }
    }
    for (uint32_t shape = 0; shape < 3; ++shape) {
        const uint32_t modifiers[] = {0,1,64,4,32};
        for (uint32_t index = 0; index < 5; ++index) {
            reset_private();
            owner.DeviceErrors = 0; owner.Decoder = 0;
            cancel_profile = 1;
            owner.TextState = expected_modifiers = modifiers[index];
            owner.Line.Length = shape == 0 ? 0 : shape == 1 ? 2 : 256;
            memset(owner.Line.Data,'x',owner.Line.Length);
            if (shape == 1) memcpy(owner.Line.Data,"\xc3\xa8",2);
            queue[count++] = KERNEL_PAYLOAD_DATA | 0x76;
            queue[count++] = KERNEL_PAYLOAD_DATA | 0x76;
            queue[count++] = KERNEL_PAYLOAD_DATA | 0xf0;
            queue[count++] = KERNEL_PAYLOAD_DATA | 0x76;
            x86_ps2_consume();
            assert(cancelled == 2 && submitted == (uint32_t)(expected_modifiers == 0) && closed == 1);
            assert(owner.ConsumerDone && !owner.Failed && !owner.Resyncs && owner.Releases == 1);
        }
    }
    puts("PASS: PS2 consumer 277 scenarios; fault recovery, Esc cancellation/repeat/release, real C/Rust; queue/scheduler MOCK");
    return 0;
#endif
}
