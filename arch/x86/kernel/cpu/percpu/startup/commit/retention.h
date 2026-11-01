#ifndef OS_X86_AP_JOINT_RETENTION_H
#define OS_X86_AP_JOINT_RETENTION_H
#include <stdint.h>
int xj_retained(const void *resource);
int xj_context_retained(uint32_t index);
uint32_t xj_phase(const void *owner);
#endif
