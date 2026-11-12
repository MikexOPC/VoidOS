#ifndef OS_X86_PROCESS_RESIDENT_H
#define OS_X86_PROCESS_RESIDENT_H
#include "process.h"

// One trusted service on the BSP. Call with IF=0 from the kernel dispatcher.
// Window and timer must outlive the service. Each operation returns the CPU to the ordinary IDT.
int x86_process_resident_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer,
    const X86_PROCESS_IMAGE *image);
int x86_process_resident_request(const unsigned char request[2], unsigned char response[2],
    BOOT_U32 input_mask, X86_PROCESS_RESULT *result, int retain_on_failure);
int x86_process_resident_close(void);
#endif
