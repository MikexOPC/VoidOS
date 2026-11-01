#ifndef OS_X86_AP_PRIVATE_FRAME_H
#define OS_X86_AP_PRIVATE_FRAME_H
#include "return.h"

// ====== private_frame_abi ======

typedef struct {
    XS_ENTRY_RECORD  Record;
    unsigned char  Padding[24], Idt[512], Idtr[10], PointerPadding[6];
    uint32_t  Mxcsr, Reserved, Phase, Error;
    unsigned char  BridgePadding[16], BridgeGdt[32], BridgePointer[10], ReturnPadding[6];
    XS_RETURN_PLAN  Return;
    unsigned char  Tail[224];
} XS_PRIVATE_FRAME;

_Static_assert(sizeof(XS_PRIVATE_FRAME) == 1024, "Private frame bytes");
_Static_assert(offsetof(XS_PRIVATE_FRAME, Idt) == 128 && offsetof(XS_PRIVATE_FRAME, Idtr) == 640 &&
    offsetof(XS_PRIVATE_FRAME, Mxcsr) == 656 && offsetof(XS_PRIVATE_FRAME, Phase) == 664 &&
    offsetof(XS_PRIVATE_FRAME, Error) == 668 && offsetof(XS_PRIVATE_FRAME, BridgeGdt) == 688 &&
    offsetof(XS_PRIVATE_FRAME, BridgePointer) == 720 && offsetof(XS_PRIVATE_FRAME, Return) == 736, "Private assembly offsets");

// ====== private_assembly_interface ======

extern const unsigned char xs_private_entry[];
#endif
