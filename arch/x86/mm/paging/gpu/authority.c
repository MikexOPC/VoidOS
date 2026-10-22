#include "authority.h"
static const X86_PAGING_GPU_AUTH *registered;
static X86_PAGING_GPU_CHECK registered_check;

// ====== x86_paging_gpu_owner ======

const X86_PAGING_GPU_AUTH *x86_paging_gpu_owner(void)
{
    return registered;
}

// ====== x86_paging_gpu_register ======

int x86_paging_gpu_register(const X86_PAGING_GPU_AUTH *expected, const X86_PAGING_GPU_AUTH *next,
    X86_PAGING_GPU_CHECK check)
{
    if (registered != expected || (!next && check) ||
        (next && (!check || expected || next->Self != (BOOT_U64)next || next->Ready != 1 || !check(next)))) return 0;
    registered_check=check;
    registered=next;
    return 1;
}

// ====== x86_paging_gpu_live ======

int x86_paging_gpu_live(void)
{
    return !registered || (registered_check && registered_check(registered));
}
