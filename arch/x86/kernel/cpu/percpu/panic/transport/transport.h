#ifndef OS_X86_PANIC_TRANSPORT_H
#define OS_X86_PANIC_TRANSPORT_H
#include "../panic.h"
typedef struct {
    int (*Read)(void *, uint32_t, uint32_t *);
    int (*Write)(void *, uint32_t, uint32_t);
    int (*Now)(void *, uint32_t *);
    void (*Pause)(void *);
    void *Context;
    uint32_t Mask, Ticks, MaxPolls;
} XP_PANIC_TRANSPORT;
uint64_t xp_panic_nmi_bits(uint32_t id);
int xp_panic_transport_run(uint32_t index, const uint32_t *ids, uint32_t count, const XP_PANIC_TRANSPORT *transport);
#endif
