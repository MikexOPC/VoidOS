#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/console/line/line.h"
#include "kernel/input/keyboard/text.h"
static unsigned checks;
#define CHECK(e) do { ++checks; assert(e); } while (0)

// ====== bounded_editor_integer_abi ======

int main(void)
{
    struct {
        uint64_t         Before;
        KERNEL_PS2_LINE   Line;
        uint64_t         After;
    } guarded = {0};
    KERNEL_PS2_LINE *line = &guarded.Line, snapshot;
    guarded.Before = 0x123456789abcdef0ULL; guarded.After = ~guarded.Before;
    kernel_ps2_line_clear(line);
    CHECK(kernel_ps2_line_step(NULL,0x61) == KERNEL_PS2_LINE_INVALID);
    kernel_ps2_line_clear(NULL);
    for (unsigned repetition = 0; repetition < 10000; ++repetition) {
        CHECK(kernel_ps2_line_step(line,0xe8) == KERNEL_PS2_LINE_APPEND);
        CHECK(line->Length == 2 && !memcmp(line->Data,"\xc3\xa8",3));
        CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_BACKSPACE) == KERNEL_PS2_LINE_ERASE);
        CHECK(!line->Length && !line->Data[0] && !line->Data[1]);
        CHECK(kernel_ps2_line_step(line,0x20ac) == KERNEL_PS2_LINE_APPEND);
        CHECK(kernel_ps2_line_step(line,0x1f600) == KERNEL_PS2_LINE_APPEND);
        CHECK(line->Length == 7 && !memcmp(line->Data,"\xe2\x82\xac\xf0\x9f\x98\x80",8));
        CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_BACKSPACE) == KERNEL_PS2_LINE_ERASE);
        CHECK(line->Length == 3 && !line->Data[3] && !line->Data[6]);
        CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_ENTER) == KERNEL_PS2_LINE_SUBMIT);
        CHECK(line->Length == 3); // Retain until delivery is confirmed.
        kernel_ps2_line_clear(line);
        for (unsigned index = 0; index < sizeof(line->Data); ++index) CHECK(!line->Data[index]);
    }
    for (unsigned index = 0; index < 255; ++index)
        CHECK(kernel_ps2_line_step(line,0x61) == KERNEL_PS2_LINE_APPEND);
    snapshot = *line;
    CHECK(kernel_ps2_line_step(line,0xe8) == KERNEL_PS2_LINE_FULL);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    CHECK(kernel_ps2_line_step(line,0x62) == KERNEL_PS2_LINE_APPEND && line->Length == 256);
    snapshot = *line;
    CHECK(kernel_ps2_line_step(line,0x63) == KERNEL_PS2_LINE_FULL);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_BACKSPACE) == KERNEL_PS2_LINE_ERASE);
    CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_BACKSPACE) == KERNEL_PS2_LINE_ERASE);
    CHECK(kernel_ps2_line_step(line,0xe8) == KERNEL_PS2_LINE_APPEND && line->Length == 256);
    CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_TAB) == KERNEL_PS2_LINE_NONE);
    snapshot = *line;
    CHECK(kernel_ps2_line_step(line,0x1b) == KERNEL_PS2_LINE_INVALID);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    line->Length = UINT32_MAX; snapshot = *line;
    CHECK(kernel_ps2_line_step(line,0x61) == KERNEL_PS2_LINE_INVALID);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    kernel_ps2_line_clear(line); line->Data[0] = 1; snapshot = *line;
    CHECK(kernel_ps2_line_step(line,0x61) == KERNEL_PS2_LINE_INVALID);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    kernel_ps2_line_clear(line); line->Length = 2; memcpy(line->Data,"\xc0\xaf",2);
    snapshot = *line;
    CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_BACKSPACE) == KERNEL_PS2_LINE_INVALID);
    CHECK(!memcmp(line,&snapshot,sizeof(snapshot)));
    kernel_ps2_line_clear(line);
    CHECK(kernel_ps2_line_step(line,KERNEL_PS2_TEXT_ENTER) == KERNEL_PS2_LINE_SUBMIT);
    CHECK(!line->Length && !line->Data[0]);
    CHECK(guarded.Before == 0x123456789abcdef0ULL && guarded.After == ~guarded.Before);
    printf("PASS: line editor C/Rust %u checks, bounds, UTF-8, canary and reuse\n",checks);
    return 0;
}
