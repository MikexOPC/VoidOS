#ifndef OS_X86_AP_JOINT_REGISTRY_H
#define OS_X86_AP_JOINT_REGISTRY_H
#include "../retention.h"
int xj_latch(const void *owner, const void *image, const void *transition, const void *resources, uint32_t count);
int xj_finish(const void *owner, uint32_t phase);
int xj_active(void);
#endif
