#include "arch/x86/kernel/cpu/percpu/panic/panic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
static XP_PANIC_RECORD records[64];
static HANDLE start;
typedef struct { uint64_t Reason; int Result; } PRODUCER;

// ====== input_private ======

static XP_PANIC_INPUT input_private(uint64_t reason) {
    XP_PANIC_INPUT input = {0};
    input.Kind = 2; input.Vector = 14; input.Error = 17; input.Cs = 8;
    input.Flags = 2; input.Ss = 16; input.Reason = reason;
    for (unsigned int i = 0; i < 16; ++i) input.Gpr[i] = reason + i;
    return input;
}

// ====== producer_private ======

static DWORD WINAPI producer_private(void *context) {
    PRODUCER *producer = context;
    XP_PANIC_INPUT input = input_private(producer->Reason);
    assert(WaitForSingleObject(start, 10000) == WAIT_OBJECT_0);
    producer->Result = xp_panic_record_capture(&records[63], &input);
    return 0;
}

// ====== main ======

int main(void) {
    assert(!xp_panic_configure(65));
    assert(xp_panic_profile_valid(0xfee00900, 0xfee00000, 6, 6, 48, 48, 1));
    assert(xp_panic_profile_valid(0xfee00800, 0xfee00000, 6, 6, 48, 48, 0));
    assert(!xp_panic_profile_valid(0xfee00d00, 0xfee00000, 6, 6, 48, 48, 1));
    assert(!xp_panic_profile_valid(0xfee00900, 0xfee00000, 6, 6, 48, 48, 0));
    assert(!xp_panic_profile_valid(0xfee00900, 0xfee00000, 6, 7, 48, 48, 1));
    assert(!xp_panic_profile_valid(0xfee00900, 0xfee00000, 6, 6, 48, 52, 1));
    assert(!xp_panic_profile_valid(0xfee00901, 0xfee00000, 6, 6, 48, 48, 1));
    assert(!xp_panic_record_initialize((XP_PANIC_RECORD *)(uintptr_t)xp_panic_state_address(), 0));
    assert(!xp_panic_record_initialize(NULL, 0));
    assert(!xp_panic_record_initialize((XP_PANIC_RECORD *)(uintptr_t)(UINTPTR_MAX - 63), 0));
    records[0].Reserved[0] = 1;
    assert(!xp_panic_record_initialize(&records[0], 0)); records[0].Reserved[0] = 0;
    for (uint32_t i = 0; i < 64; ++i) assert(xp_panic_record_initialize(&records[i], i));
    assert(!xp_panic_record_initialize(&records[0], 0));
    assert(!xp_panic_record_initialize((XP_PANIC_RECORD *)((char *)&records[0] + 64), 0));
    assert(xp_panic_record_retire(&records[63]) && !xp_panic_record_retire(&records[63]));
    assert(xp_panic_record_initialize(&records[63], 63));
    assert(xp_panic_configure(64) && !xp_panic_record_retire(&records[0]));
    XP_PANIC_INPUT input = input_private(UINT64_MAX - 100), output;
    memset(&output, 0xa5, sizeof(output)); XP_PANIC_INPUT unchanged = output;
    assert(!xp_panic_record_snapshot(&records[0], &output) && !memcmp(&output, &unchanged, sizeof(output)));
    assert(!xp_panic_record_capture(&records[0], (XP_PANIC_INPUT *)&records[0]));
    input.Kind = 4; assert(!xp_panic_record_capture(&records[0], &input)); input.Kind = 2;
    assert(xp_panic_record_state(0) == XP_PANIC_EMPTY);
    assert(xp_panic_record_capture(&records[0], &input));
    assert(!xp_panic_record_capture(&records[0], &input));
    assert(xp_panic_record_snapshot(&records[0], &output) && !memcmp(&output, &input, sizeof(input)));
    XP_PANIC_RECORD fake = records[0]; fake.Self = (uintptr_t)&fake;
    assert(!xp_panic_record_snapshot(&fake, &output));
    assert(!xp_panic_record_snapshot((const XP_PANIC_RECORD *)(uintptr_t)0x10000, &output));
    assert(!xp_panic_record_snapshot(&records[0], (XP_PANIC_INPUT *)(uintptr_t)xp_panic_state_address()));
    assert(!xp_panic_record_snapshot(&records[0], (XP_PANIC_INPUT *)&records[1]));
    records[0].Reserved[0] = 1;
    assert(!xp_panic_record_snapshot(&records[0], &output)); records[0].Reserved[0] = 0;
    records[0].Data.Cs = 3;
    assert(!xp_panic_record_snapshot(&records[0], &output)); records[0].Data.Cs = 8;
    assert(xp_panic_record_claim(1));
    assert(!xp_panic_record_snapshot(&records[1], &output) && !xp_panic_acknowledge(1));
    for (uint64_t vector = 0; vector < 32; ++vector) {
        assert(xp_panic_input_valid(2, vector, 0, 8, 2, 0));
        assert(xp_panic_input_valid(2, vector, 0, 8, 2, 16));
        assert(!xp_panic_input_valid(2, vector, 0, 8, 2, 8));
        assert(!xp_panic_input_valid(2, vector, 0, 3, 2, 16));
        assert(!xp_panic_input_valid(2, vector, 0, 8, 0, 16));
    }
    assert(!xp_panic_input_valid(3, 14, 0, 8, 2, 16));
    assert(!xp_panic_input_valid(2, 8, 1, 8, 2, 16));
    HANDLE jobs[8]; PRODUCER producers[8] = {0};
    start = CreateEventW(NULL, TRUE, FALSE, NULL); assert(start);
    for (unsigned int i = 0; i < 8; ++i) {
        producers[i].Reason = i + 100;
        jobs[i] = CreateThread(NULL, 0, producer_private, &producers[i], 0, NULL); assert(jobs[i]);
    }
    assert(SetEvent(start));
    assert(WaitForMultipleObjects(8, jobs, TRUE, 10000) == WAIT_OBJECT_0);
    unsigned int winners = 0;
    for (unsigned int i = 0; i < 8; ++i) { winners += producers[i].Result; assert(CloseHandle(jobs[i])); }
    assert(CloseHandle(start) && winners == 1);
    assert(xp_panic_record_snapshot(&records[63], &output));
    assert(output.Reason >= 100 && output.Reason < 108);
    for (unsigned int i = 0; i < 16; ++i) assert(output.Gpr[i] == output.Reason + i);
    puts("PANIC_RECORDS_PASS: live ownership, alias/bounds refusal, publication, incomplete records, eight competing C producers, all OS handles closed");
    return 0;
}
