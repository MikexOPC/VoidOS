#include "protection.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>

// Intel SDM 3A §2.3: EFLAGS.AC is bit 18.
#define X86_EFLAGS_AC (1ULL << 18)

void x86_user_cpu_features(BOOT_U32 *max_leaf, BOOT_U32 *leaf7_ebx);
BOOT_U32 x86_user_protection_apply(BOOT_U64 bits);
BOOT_U64 x86_user_protection_bits(BOOT_U32 max_leaf, BOOT_U32 leaf7_ebx);
BOOT_U32 x86_user_mode_permitted(BOOT_U64 bits);

static BOOT_U64 published;
static volatile BOOT_U32 allowed;

// ====== cpu_bits ======

static BOOT_U64 cpu_bits(void)
{
    BOOT_U32 max_leaf, leaf7_ebx;
    x86_user_cpu_features(&max_leaf, &leaf7_ebx);
    return x86_user_protection_bits(max_leaf, leaf7_ebx);
}

// ====== x86_user_protection_boot ======

int x86_user_protection_boot(void)
{
    BOOT_U64 bits = cpu_bits();
    if ((__readeflags() & X86_EFLAGS_AC) || !x86_user_protection_apply(bits)) return 0;
    published = bits;
    allowed = x86_user_mode_permitted(bits);
    if (!kernel_text("KERNEL_USER_PROTECTION: ") || !kernel_number(bits) || !kernel_text("\n")) return 0;
    return allowed ? kernel_log(0, "KERNEL_USER_MODE_ALLOWED") : kernel_log(2, "USER_MODE_DISABLED");
}

// ====== x86_user_protection_ap ======

int x86_user_protection_ap(void)
{
    BOOT_U64 bits = cpu_bits() & published;
    if (bits != published) allowed = 0;
    return !(__readeflags() & X86_EFLAGS_AC) && x86_user_protection_apply(bits);
}

// ====== x86_user_mode_allowed ======

int x86_user_mode_allowed(void)
{
    return allowed != 0;
}
