#ifndef OS_X86_CONSOLE_NOTIFY_H
#define OS_X86_CONSOLE_NOTIFY_H
#include <stdint.h>
#define XCN_VECTOR 0x43U
#define XCN_EVENT 7U

typedef struct {
    uint64_t  Self, Token, Binding, Active;
    uint64_t  Sent, Delivered, Eois, Drains, Closed, Denied;
    uint64_t  Snapshot[6];
} XCN_RECORD;
_Static_assert(sizeof(XCN_RECORD) == 128, "Console notification diagnostic ABI");
extern XCN_RECORD xcn_record;
extern volatile uint32_t xcn_wake[2];
int xcn_icr_low(uint32_t command);
int xcn_prepare(uint64_t token);
int xcn_send(uint32_t producer);
int xcn_poll(void);
int xcn_close(int queue_empty);
int xcn_release_allowed(void);
uint64_t kernel_console_ap_notify(uint32_t operation, uint32_t cpu, uint64_t token,
    uint32_t producer, uint64_t value);
#endif
