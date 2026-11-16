#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/scheduler/payload.h"

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
int kernel_text(const char *text) { assert(text); return 1; }
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
}
