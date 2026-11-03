#ifndef OS_TEST_AP_SERVICE_PROBE_H
#define OS_TEST_AP_SERVICE_PROBE_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/regions/mapping.h"
__declspec(noreturn) void xis_service_probe(X86_PAGE_WINDOW *window, X86_REGION_MAPPER *regions);
#endif
