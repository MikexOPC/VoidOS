#ifndef X86_SCHEDULER_ADMISSION_H
#define X86_SCHEDULER_ADMISSION_H
#include <stdint.h>
#define XAD_INVALID UINT64_MAX
#define XAD_OFFERED 1ULL
#define XAD_CLAIMED 2ULL
#define XAD_ADMITTED 3ULL
#define XAD_FAILED 4ULL

// ====== admission_protocol ======

uint32_t xad_configure(uint32_t count, uint64_t group, uint32_t caller);
uint32_t xad_offer(uint32_t cpu, uint32_t caller, uint64_t token, uint64_t group);
uint64_t xad_claim(uint32_t cpu, uint32_t caller, uint64_t group);
uint32_t xad_admit(uint32_t cpu, uint32_t caller, uint64_t token, uint64_t group);
uint32_t xad_fail(uint32_t cpu, uint32_t caller, uint64_t token, uint64_t group);
uint64_t xad_value(uint32_t cpu, uint64_t group);

typedef __declspec(align(64)) struct {
    uint64_t  Self, Group, Token, Cpu, Rsp, Local, Root, Irq;
} XAD_NATIVE_RECORD;
_Static_assert(sizeof(XAD_NATIVE_RECORD) == 64, "Admission evidence ABI");
extern volatile XAD_NATIVE_RECORD xad_native_record;
void xad_native_poll(void);
#endif
