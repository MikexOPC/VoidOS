#ifndef OS_X86_PANIC_H
#define OS_X86_PANIC_H
#include <stdint.h>
#include <stddef.h>
#define XP_PANIC_STATE_BYTES 640U
#define XP_PANIC_EMPTY 0U
#define XP_PANIC_WRITING 1U
#define XP_PANIC_COMPLETE 2U
#define XP_PANIC_INVALID 3U
#define XP_PANIC_MISSING 1ULL
#define XP_PANIC_CLOCK_FAILED 2ULL
#define XP_PANIC_TRANSPORT_FAILED 4ULL
#define XP_PANIC_POLL_LIMIT 8ULL
#define XP_PANIC_DEADLINE 16ULL
#define XP_PANIC_READY 0U
#define XP_PANIC_KNOWN 1U
#define XP_PANIC_ONLINE 2U
#define XP_PANIC_OWNER 3U
#define XP_PANIC_TARGETS 4U
#define XP_PANIC_ACK 5U
#define XP_PANIC_MISSED 6U
#define XP_PANIC_PHASE 7U
#define XP_PANIC_OUTCOME 8U

typedef struct {
    uint64_t  Kind, Frame, Reason, Cr2, Cr3;
    uint64_t  Vector, Error, Rip, Cs, Flags, Rsp, Ss;
    uint64_t  Gpr[16];
} XP_PANIC_INPUT;
typedef struct {
    _Alignas(64) uint64_t Self;
    uint64_t  Version, Index;
    XP_PANIC_INPUT  Data;
    uint64_t  Reserved[33];
} XP_PANIC_RECORD;
_Static_assert(sizeof(XP_PANIC_INPUT) == 224, "Panic payload ABI");
_Static_assert(sizeof(XP_PANIC_RECORD) == 512 && _Alignof(XP_PANIC_RECORD) == 64, "Panic record ABI");
_Static_assert(offsetof(XP_PANIC_RECORD, Data) == 24 && offsetof(XP_PANIC_INPUT, Gpr) == 96, "Panic frame/GPR ABI");
uint32_t xp_panic_configure(uint32_t count);
uint32_t xp_panic_admit(uint32_t index);
uint32_t xp_panic_elect(uint32_t index);
uint32_t xp_panic_record_claim(uint32_t index);
uint32_t xp_panic_record_publish(uint32_t index);
uint32_t xp_panic_record_state(uint32_t index);
uint32_t xp_panic_acknowledge(uint32_t index);
uint32_t xp_panic_finish(uint32_t index, uint64_t outcome);
uint64_t xp_panic_value(uint32_t field);
uint64_t xp_panic_state_address(void);
int xp_panic_record_initialize(XP_PANIC_RECORD *record, uint32_t index);
int xp_panic_record_retire(XP_PANIC_RECORD *record);
int xp_panic_record_capture(XP_PANIC_RECORD *record, const XP_PANIC_INPUT *input);
int xp_panic_record_snapshot(const XP_PANIC_RECORD *record, XP_PANIC_INPUT *output);
uint32_t xp_panic_input_valid(uint64_t kind, uint64_t vector, uint64_t error, uint64_t cs, uint64_t flags, uint64_t ss);
uint32_t xp_panic_profile_valid(uint64_t base, uint64_t physical, uint64_t pat, uint64_t expected_pat, uint32_t bits, uint32_t expected_bits, uint32_t bsp);
#endif
