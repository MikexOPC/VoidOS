#ifndef OS_TEST_AP_SERVICE_PROBE_H
#define OS_TEST_AP_SERVICE_PROBE_H
#include "arch/x86/mm/window/page.h"
#include "kernel/startup/startup.h"
__declspec(noreturn) void xis_service_probe(const KERNEL_STARTUP *startup);
#endif
