#ifndef OS_X86_AP_JOINT_COMMIT_H
#define OS_X86_AP_JOINT_COMMIT_H
#include "../installation/install.h"
#include "retention.h"

// ====== joint_commit_contract ======

typedef struct {
    void  *Context;
    const void  *Resources;
    uint32_t  Cpus, Bytes;
    int (*Check)(void *context, XB_INSTALLATION *image, uint32_t cpus);
    int (*Seal)(void *context, uint32_t cpus);
    int (*Verify)(void *context, uint32_t cpus);
} XJ_OPERATIONS;

typedef struct {
    uint64_t  Self;
    XB_INSTALLATION  *Image;
    XJ_OPERATIONS  Operations;
    uint32_t  State, Failure;
} XJ_GROUP;
_Static_assert(sizeof(XJ_GROUP) == 72 && offsetof(XJ_GROUP, State) == 64, "Joint monitor ABI");
int xj_commit(XJ_GROUP *group, XB_INSTALLATION *image, const XJ_OPERATIONS *operations);
int xj_native_operations(XJ_OPERATIONS *output);
#endif
