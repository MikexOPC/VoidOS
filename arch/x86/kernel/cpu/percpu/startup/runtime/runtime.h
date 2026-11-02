#ifndef OS_X86_AP_RUNTIME_H
#define OS_X86_AP_RUNTIME_H
#include "../../transition/transition.h"
#include "arch/x86/mm/devices/lapic.h"

int x86_ap_runtime_start(X86_AP_TRANSITION *transition, const X86_LAPIC_MAPPING *lapic);
#endif
