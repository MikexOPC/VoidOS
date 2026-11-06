#ifndef TEST_CONSOLE_AP_CHANNEL_H
#define TEST_CONSOLE_AP_CHANNEL_H
#include "kernel/console/video/requests/queue.h"

typedef struct {
    uint64_t  Self, Root, Token, Full[2], Sent[2], Consumed[2];
    uint64_t  Completed, Closed, Denied, Budget, Held, Final;
} KCA_RECORD;
_Static_assert(sizeof(KCA_RECORD) == 120, "AP console diagnostic ABI");
extern KCA_RECORD kca_record;
void kca_prepare(void);
void kca_produce(uint32_t index);
void kca_pump(void);
void kca_finish(void);
int kca_release_allowed(void);
void kca_report(void);
void xat_io_yield(void);
uint64_t kernel_console_ap_channel(uint32_t operation, uint32_t cpu, uint64_t token, uint32_t producer);
#endif
