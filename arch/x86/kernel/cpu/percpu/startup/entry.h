#ifndef OS_X86_AP_ENTRY_RECORD_H
#define OS_X86_AP_ENTRY_RECORD_H
#include <stdint.h>
#include <stddef.h>

// ====== entry_record ======

#define XS_ENTRY_VERSION 1U

typedef struct {
    uint32_t  Version, Bytes, Index, ApicId;
    uint64_t  Token, Code, Data, TransitionRoot, KernelRoot;
    uint64_t  StackTop, Descriptors, Local, Entry, Pat, Reserved;
} XS_ENTRY_RECORD;

typedef struct {
    uint32_t  Index, ApicId, PhysicalBits, Reserved;
    uint64_t  Token, Code, Data, TransitionRoot, KernelRoot, Local, Entry, Pat;
} XS_ENTRY_INPUT;

_Static_assert(sizeof(XS_ENTRY_RECORD) == 104 && _Alignof(XS_ENTRY_RECORD) == 8, "Entry record ABI");
_Static_assert(offsetof(XS_ENTRY_RECORD, Token) == 16 && offsetof(XS_ENTRY_RECORD, TransitionRoot) == 40 &&
    offsetof(XS_ENTRY_RECORD, StackTop) == 56 && offsetof(XS_ENTRY_RECORD, Entry) == 80 &&
    offsetof(XS_ENTRY_RECORD, Reserved) == 96, "Entry offsets ABI");

uint32_t xs_entry_valid(XS_ENTRY_RECORD record, uint32_t bits);
uint32_t xs_entry_claim(XS_ENTRY_RECORD record, uint32_t bits, uint32_t observed_id);
int xs_entry_build(XS_ENTRY_RECORD *output, XS_ENTRY_INPUT input);
#endif
