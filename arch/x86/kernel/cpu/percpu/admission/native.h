#ifndef OS_X86_AP_ADMISSION_NATIVE_H
#define OS_X86_AP_ADMISSION_NATIVE_H
#include "group.h"
#include "../startup/commit/commit.h"

// ====== sealed_group_publication ======

int xag_native_publish(const XJ_GROUP *group);
int xag_native_close(void);
#endif
