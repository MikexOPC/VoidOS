#ifndef OS_X86_AP_RETURN_H
#define OS_X86_AP_RETURN_H
#include "../entry.h"

// ====== private_return_plan ======

typedef struct {
    uint32_t  Version, Bytes;
    uint64_t  Token, Stack, Entry, Idtr, Local, Root, Reserved;
} XS_RETURN_PLAN;
_Static_assert(sizeof(XS_RETURN_PLAN) == 64 && _Alignof(XS_RETURN_PLAN) == 8 &&
    offsetof(XS_RETURN_PLAN, Idtr) == 32 && offsetof(XS_RETURN_PLAN, Reserved) == 56, "Return plan ABI");
uint32_t xs_return_valid(XS_RETURN_PLAN plan, XS_ENTRY_RECORD record, uint32_t bits);
int xs_return_build(XS_RETURN_PLAN *output, XS_ENTRY_RECORD record, uint64_t entry, uint64_t idtr, uint32_t bits);
extern const unsigned char xs_private_resume[];
#endif
