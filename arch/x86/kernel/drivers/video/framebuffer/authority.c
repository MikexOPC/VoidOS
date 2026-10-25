#include "mapping.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== x86_fb_authority ======

int x86_fb_authority(const X86_PAGING_VIDEO_AUTH *owner)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root;
    if (!owner || ((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner ||
        owner->Ready != 1 || owner->Region.Owner != (BOOT_U64)owner ||
        !owner->Region.TableCount || owner->Region.TableCount > X86_PAGING_VIDEO_TABLES ||
        !x86_exception_environment() || !x86_fb_cpu(&cpu) ||
        cpu.Root != owner->Root || cpu.Pat != owner->Region.Pat || cpu.PhysicalBits != owner->PhysicalBits ||
        !x86_paging_video_policy(owner->Region.Virtual, owner->Region.Physical, owner->Region.Pages,
            owner->Region.AperturePages, owner->Region.Cache, cpu.Pat, cpu.PhysicalBits)
        ) return 0;
    root = x86_fb_root();
    return root && (root[X86_BOOT_RECURSIVE_SLOT] & ~0x60ULL) == (owner->Root | 0x8000000000000003ULL) &&
        (root[X86_FB_SLOT] & ~0x60ULL) == (owner->Tables[0].Base | 3);
}
