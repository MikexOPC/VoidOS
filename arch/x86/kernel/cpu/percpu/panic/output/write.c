#include "output.h"
typedef struct { uint32_t Start, Polls, Bytes, Status; } CURSOR;
#define PANIC_TEXT(s, c, literal) text_private(s, c, literal, (uint32_t)(sizeof(literal) - 1))
#define PANIC_FIELD(s, c, literal, value) field_private(s, c, literal, (uint32_t)(sizeof(literal) - 1), value)

// ====== character_private ======

// QEMU 11.1.0 hw/char/serial.c: LCR +3 bit 7 DLAB; LSR +5 bit 5 THRE; THR +0 with DLAB clear.
static int character_private(const XP_PANIC_SINK *s, volatile CURSOR *c, uint8_t ch) {
    if (c->Status != 1) return 0;
    if (c->Bytes >= s->MaxBytes) { c->Status = 3; return 0; }
    while (c->Polls < s->MaxPolls) {
        uint32_t now; ++c->Polls;
        if (!s->Now(s->Context, &now)) { c->Status = 6; return 0; }
        if (((now - c->Start) & s->Mask) >= s->Ticks) { c->Status = 4; return 0; }
        uint8_t lcr = s->Read(s->Context, (uint16_t)(s->Port + 3));
        uint8_t lsr = s->Read(s->Context, (uint16_t)(s->Port + 5));
        if ((lcr & 0x80) || lsr == 255) { c->Status = 2; return 0; }
        if (lsr & 0x20) {
            if (!s->Write(s->Context, s->Port, ch)) { c->Status = 2; return 0; }
            ++c->Bytes; return 1;
        }
        s->Pause(s->Context);
    }
    c->Status = 5; return 0;
}

// ====== text_private ======

static int text_private(const XP_PANIC_SINK *s, volatile CURSOR *c, const char *text, uint32_t bytes) {
    if (bytes >= 64) { c->Status = 7; return 0; }
    for (uint32_t index = 0; index < bytes; ++index) {
        uint8_t ch = (uint8_t)text[index];
        if (ch == '\n' && !character_private(s, c, '\r')) return 0;
        if (!character_private(s, c, ch)) return 0;
    }
    return 1;
}

// ====== field_private ======

static int field_private(const XP_PANIC_SINK *s, volatile CURSOR *c, const char *label, uint32_t label_bytes, uint64_t value) {
    if (!text_private(s, c, label, label_bytes) || !PANIC_TEXT(s, c, ": 0x")) return 0;
    for (uint32_t nibble = 0; nibble < 16; ++nibble)
        if (!character_private(s, c, (uint8_t)"0123456789abcdef"[(value >> (60 - nibble * 4)) & 15])) return 0;
    return PANIC_TEXT(s, c, "\n");
}

// ====== xp_panic_output_report ======

uint32_t xp_panic_output_report(uint32_t index, const XP_PANIC_RECORD *record, const XP_PANIC_SINK *s) {
    if (!s || !s->Read || !s->Write || !s->Now || !s->Pause || s->Port > 65528 ||
        (s->Mask != 0xffffffU && s->Mask != UINT32_MAX) || !s->Ticks || s->Ticks >= s->Mask / 2 ||
        (!s->MaxPolls || s->MaxPolls > 200000U) || !s->MaxBytes || s->MaxBytes > 2048 || xp_panic_value(XP_PANIC_OWNER) != (uint64_t)index + 1 ||
        xp_panic_value(XP_PANIC_PHASE) != 2) return 7;
    uint32_t start;
    if (!s->Now(s->Context, &start)) return 6;
    volatile CURSOR cursor;
    cursor.Start = start & s->Mask; cursor.Polls = 0; cursor.Bytes = 0; cursor.Status = 1;
    XP_PANIC_INPUT input;
    int complete = xp_panic_record_snapshot(record, &input);
    if (!PANIC_TEXT(s, &cursor, "\n# ============ panic SMP ============ #\n\n") ||
        !PANIC_TEXT(s, &cursor, "\x1b[31m[ FAIL ]\x1b[0m KERNEL_SMP_PANIC\n") ||
        !PANIC_FIELD(s, &cursor, "PANIC_OWNER", index) ||
        !PANIC_FIELD(s, &cursor, "PANIC_TARGETS", xp_panic_value(XP_PANIC_TARGETS)) ||
        !PANIC_FIELD(s, &cursor, "PANIC_ACK", xp_panic_value(XP_PANIC_ACK)) ||
        !PANIC_FIELD(s, &cursor, "PANIC_MISSING", xp_panic_value(XP_PANIC_MISSED)) ||
        !PANIC_FIELD(s, &cursor, "PANIC_OUTCOME", xp_panic_value(XP_PANIC_OUTCOME)) ||
        !PANIC_FIELD(s, &cursor, "PANIC_RECORD_COMPLETE", (uint64_t)complete)) return cursor.Status;
    if (complete && (!PANIC_FIELD(s, &cursor, "PANIC_KIND", input.Kind) ||
        !PANIC_FIELD(s, &cursor, "PANIC_VECTOR", input.Vector) || !PANIC_FIELD(s, &cursor, "PANIC_ERROR", input.Error) ||
        !PANIC_FIELD(s, &cursor, "PANIC_RIP", input.Rip) || !PANIC_FIELD(s, &cursor, "PANIC_RSP", input.Rsp) ||
        !PANIC_FIELD(s, &cursor, "PANIC_CR2", input.Cr2) || !PANIC_FIELD(s, &cursor, "PANIC_REASON_ADDRESS", input.Reason))) return cursor.Status;
    (void)PANIC_TEXT(s, &cursor, "KERNEL_SMP_PANIC_HALTED\n");
    return cursor.Status;
}
