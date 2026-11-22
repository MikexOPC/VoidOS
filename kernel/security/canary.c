#include "random.h"
#include "kernel/diagnostics/log.h"
#include <stdint.h>

// Default non-zero 48-bit cookie used only during early bootstrap before kernel_continue()
// seeds __security_cookie from the hardware-backed HMAC_DRBG.
#define KERNEL_SECURITY_COOKIE_DEFAULT  0x00002B992DDFA232ULL
#define KERNEL_SECURITY_COOKIE_MASK     0x0000FFFFFFFFFFFFULL
#define KERNEL_SECURITY_COOKIE_FALLBACK 0x0000A5A55A5AA5A5ULL

uintptr_t __security_cookie = (uintptr_t)KERNEL_SECURITY_COOKIE_DEFAULT;
uintptr_t __security_cookie_complement = ~(uintptr_t)KERNEL_SECURITY_COOKIE_DEFAULT;

// ====== __report_gsfailure ======

__declspec(noreturn) __declspec(safebuffers) void __cdecl __report_gsfailure(uintptr_t cookie)
{
    (void)cookie;
    kernel_panic("STACK_COOKIE_CORRUPTED");
}

// ====== __report_rangecheckfailure ======

__declspec(noreturn) __declspec(safebuffers) void __cdecl __report_rangecheckfailure(void)
{
    kernel_panic("STACK_RANGE_CHECK_FAILED");
}

// ====== __GSHandlerCheck ======

__declspec(safebuffers) int __cdecl __GSHandlerCheck(
    void *exception_record,
    void *establisher_frame,
    void *context_record,
    void *dispatcher_context
)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;
    kernel_panic("GS_HANDLER_UNEXPECTED");
}

// ====== kernel_security_cookie_validate ======

__declspec(safebuffers) void kernel_security_cookie_validate(uintptr_t cookie)
{
    if (cookie != __security_cookie || ((uint64_t)cookie >> 48) != 0ULL) {
        __report_gsfailure(cookie);
    }
}

// ====== kernel_security_cookie_init ======

__declspec(safebuffers) int kernel_security_cookie_init(void)
{
    uint64_t raw = 0ULL;
    if (!kernel_random_u64(&raw)) {
        return 0;
    }
    uint64_t cookie = raw & KERNEL_SECURITY_COOKIE_MASK;
    if (cookie == 0ULL || cookie == KERNEL_SECURITY_COOKIE_DEFAULT) {
        cookie ^= KERNEL_SECURITY_COOKIE_FALLBACK;
    }
    __security_cookie = (uintptr_t)cookie;
    __security_cookie_complement = ~(uintptr_t)cookie;
    return kernel_log(0, "KERNEL_CANARY_READY");
}
