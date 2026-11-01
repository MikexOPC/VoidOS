#include "arch/x86/kernel/cpu/percpu/panic/output/output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *Mode; uint32_t Time, Reads, Writes, Clocks, Pauses; char Text[2049]; } MODEL;
static XP_PANIC_RECORD record;

// ====== now_private ======

static int now_private(void *context, uint32_t *now) {
    MODEL *m = context; ++m->Clocks;
    if (!strcmp(m->Mode, "clock-start") || (!strcmp(m->Mode, "clock-mid") && m->Clocks > 10)) return 0;
    *now = ++m->Time; return 1;
}

// ====== read_private ======

static uint8_t read_private(void *context, uint16_t port) {
    MODEL *m = context; ++m->Reads; assert(port == 0x3fb || port == 0x3fd);
    if (!strcmp(m->Mode, "absent")) return 255;
    if (port == 0x3fb) return !strcmp(m->Mode, "dlab") ? 0x83 : 3;
    return !strcmp(m->Mode, "busy") ? 0 : 0x20;
}

// ====== write_private ======

static int write_private(void *context, uint16_t port, uint8_t value) {
    MODEL *m = context; assert(port == 0x3f8 && m->Writes < 2048);
    if (!strcmp(m->Mode, "write")) return 0;
    m->Text[m->Writes++] = (char)value;
    return 1;
}

// ====== pause_private ======

static void pause_private(void *context) { ++((MODEL *)context)->Pauses; }

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2); const char *mode = argv[1];
    assert(xp_panic_record_initialize(&record, 0));
    assert(xp_panic_configure(1) && xp_panic_admit(0) == 1);
    XP_PANIC_INPUT input = {0}; input.Kind = 1; input.Cs = 8; input.Ss = 16; input.Flags = 2; input.Reason = UINT64_MAX;
    assert(xp_panic_record_capture(&record, &input) && xp_panic_elect(0) == 1 && xp_panic_finish(0, 0));
    MODEL model = {0}; model.Mode = mode; model.Time = !strcmp(mode, "wrap") ? 0xfffffaU : 0;
    XP_PANIC_SINK sink = {read_private, write_private, now_private, pause_private, &model, 0xffffffU, 1000, 900, 2048, 0x3f8};
    sink.MaxBytes = 2049; assert(xp_panic_output_report(0, &record, &sink) == 7); sink.MaxBytes = 2048;
    if (!strcmp(mode, "bytes")) sink.MaxBytes = 4;
    if (!strcmp(mode, "deadline")) sink.Ticks = 4;
    uint32_t status = xp_panic_output_report(0, &record, &sink);
    assert(model.Clocks <= sink.MaxPolls + 1 && model.Reads <= sink.MaxPolls * 2 && model.Writes <= sink.MaxBytes);
    if (!strcmp(mode, "ok") || !strcmp(mode, "wrap")) {
        assert(status == 1 && strstr(model.Text, "KERNEL_SMP_PANIC_HALTED") &&
            strstr(model.Text, "PANIC_REASON_ADDRESS: 0xffffffffffffffff"));
    } else if (!strcmp(mode, "bytes")) assert(status == 3 && model.Writes == 4);
    else if (!strcmp(mode, "deadline")) assert(status == 4);
    else if (!strcmp(mode, "busy")) assert(status == 5 && !model.Writes && model.Pauses == sink.MaxPolls);
    else if (!strcmp(mode, "clock-start") || !strcmp(mode, "clock-mid")) assert(status == 6);
    else assert(status == 2);
    printf("PANIC_OUTPUT_PASS: %s; bounded byte/time/poll budgets, no caller-string dereference\n", mode);
    return 0;
}
