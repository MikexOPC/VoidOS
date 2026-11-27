#include "platform.h"
#include "kernel/diagnostics/log.h"
__declspec(noreturn) void kernel_main(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry);

// ====== ingresso Microsoft x64 chiamato dal trampoline persistente ====== //

__declspec(noreturn) void kernel_entry(const BOOT_START_INFO *info)
{
    (void)x86_serial_initialize();
    if (!kernel_section("ingresso kernel x86") || !kernel_log(1, "KERNEL_ENTERED"))
        kernel_panic("SERIAL_UNAVAILABLE");
    if ((BOOT_U64)info != X86_BOOT_INFO_VIRTUAL) kernel_panic("BOOT_POINTER_INVALID");
    kernel_main(info, x86_kernel_root(), (BOOT_U64)kernel_entry);
}
