#ifndef OS_X86_AP_CONTEXT_ENTRY_H
#define OS_X86_AP_CONTEXT_ENTRY_H
#include "../../startup/private/frame.h"

// ====== private_entry_contract ======

uint32_t xa_native_admit(XS_PRIVATE_FRAME *frame, uint64_t token);
uint32_t xa_native_park(XS_PRIVATE_FRAME *frame, uint64_t token);
extern const unsigned char xs_private_park[];
#endif
