#ifndef OS_X86_PANIC_OUTPUT_H
#define OS_X86_PANIC_OUTPUT_H
#include "../panic.h"
typedef struct {
    uint8_t (*Read)(void *, uint16_t);
    int (*Write)(void *, uint16_t, uint8_t);
    int (*Now)(void *, uint32_t *);
    void (*Pause)(void *);
    void *Context;
    uint32_t Mask, Ticks, MaxPolls, MaxBytes;
    uint16_t Port;
} XP_PANIC_SINK;
uint32_t xp_panic_output_report(uint32_t index, const XP_PANIC_RECORD *record, const XP_PANIC_SINK *sink);
#endif
