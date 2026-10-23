#include "session.h"
#include <string.h>
#include "../console/profile.h"
#include "arch/x86/kernel/exceptions/terminal.h"

static const X86_FB_TARGET *redirect;

// H1-A: the last fully verified paging snapshot. A pixel session repeats the full scan only when
// the paging generation moved, the snapshot or the allocator differ, the owner changed, after 64 skipped sessions
// (periodic net, H3) or after a reset at the session boundaries.
#define VERIFY_PERIOD 64U
static X86_PAGING_INVENTORY verified;
// The allocator is part of what the full scan checks (pinned table leases): a changed pool also
// forces the scan, so only page-table writes outside the APIs wait for the periodic net.
static KERNEL_PHYSICAL_ALLOCATOR verified_pool;
// CPU paging state (CR3, PAT, physical bits) is not a PTE write either: it is compared as well.
static X86_FB_CPU verified_cpu;
static BOOT_U64 verified_generation;
static const X86_FB_MAPPING *verified_owner;
static BOOT_U32 verified_ready, verified_skips;
static BOOT_U64 verify_full, verify_skipped;

// ====== x86_fb_verify_reset ======

void x86_fb_verify_reset(void) { verified_ready = 0; }

// ====== x86_fb_verify_counts ======

void x86_fb_verify_counts(BOOT_U64 *full, BOOT_U64 *skipped)
{
    if (full) *full = verify_full;
    if (skipped) *skipped = verify_skipped;
}

// ====== verify_private ======

static int verify_private(X86_FB_MAPPING *owner)
{
    BOOT_U64 generation = x86_paging_generation();
    X86_FB_CPU cpu;

    if (!x86_fb_cpu(&cpu)) return 0;

    if (verified_ready && verified_owner == owner && cpu.Root == verified_cpu.Root && cpu.Pat == verified_cpu.Pat &&
        cpu.PhysicalBits == verified_cpu.PhysicalBits && verified_generation == generation && verified_skips < VERIFY_PERIOD &&
        !memcmp(&verified, owner->Inventory, sizeof(verified)) &&
        !memcmp(&verified_pool, owner->Window->Pool, sizeof(verified_pool))
    ) {
        ++verified_skips;
        ++verify_skipped;
        return 1;
    }

    verified_ready = 0;

    if (!x86_paging_inventory_verify_video(owner->Context, owner->Window->Pool, owner->Lapic, &owner->Authority,
        x86_fb_authority, owner->Inventory)
    )
    return 0;

    ++verify_full;
    verified = *owner->Inventory;
    verified_pool = *owner->Window->Pool;
    verified_cpu = cpu;
    verified_owner = owner;
    verified_generation = x86_paging_generation();
    verified_skips = 0;
    verified_ready = 1;
    return 1;
}

// ====== x86_fb_target_redirect ======

int x86_fb_target_redirect(const X86_FB_TARGET *expected, const X86_FB_TARGET *next)
{
    if (redirect != expected ||
        (next && (expected || next->Self != (BOOT_U64)next || !next->Valid || !next->Base || (next->Base & 4095) ||
            !next->Bytes || next->Base > ~0ULL - next->Bytes || next->Reserved ||
            !next->Width || !next->Height || next->Stride < next->Width ||
            (BOOT_U64)next->Stride * next->Height * 4 > next->Bytes || !next->Valid(next)))
    )
    return 0;

    redirect = next;
    return 1;
}

// ====== x86_fb_target ======

const X86_FB_TARGET *x86_fb_target(void) { return redirect; }

// ====== x86_fb_surface_geometry ======

const OS_FRAMEBUFFER *x86_fb_surface_geometry(const X86_FB_MAPPING *owner, OS_FRAMEBUFFER *scratch)
{
    if (!owner || !owner->Context || !scratch) return 0;

    if (!redirect) return &owner->Context->Info.Framebuffer;

    *scratch = owner->Context->Info.Framebuffer;
    scratch->Base = redirect->Base;
    scratch->Width = redirect->Width;
    scratch->Height = redirect->Height;
    scratch->Stride = redirect->Stride;
    scratch->Format = 1;
    scratch->Used = (BOOT_U64)redirect->Stride * redirect->Height * 4;
    scratch->Capacity = redirect->Bytes;
    return scratch;
}

// ====== x86_fb_pixels_begin ======

int x86_fb_pixels_begin(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, X86_FB_PIXEL_SESSION *session)
{
    const OS_FRAMEBUFFER *fb;
    OS_FRAMEBUFFER geometry;
    BOOT_U64 first, last, mapped, start, base;
    if (!session || !owner || ((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner ||
        owner->Ready != 2 || owner->Published != 1 || !owner->Context || !owner->Window ||
        !owner->Window->Pool || !owner->Lapic || !owner->Inventory ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        owner->Allocated != owner->Authority.Region.TableCount ||
        owner->Pinned != (1U << owner->Allocated) - 1 || !x86_exception_environment() ||
        !x86_page_window_idle_valid(owner->Window)) return 0;
    fb = x86_fb_surface_geometry(owner, &geometry);
    if (!fb || !x86_fb_rect_valid(((BOOT_U64)fb->Height << 32) | fb->Width,
        ((BOOT_U64)y << 32) | x, ((BOOT_U64)height << 32) | width)) return 0;
    first = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used, x, y);
    last = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used,
        x + width - 1, y + height - 1);
    if (first == ~0ULL || last == ~0ULL || !owner->Authority.Region.Pages ||
        owner->Authority.Region.Pages > 4321) return 0;
    if (redirect) {
        // Redirected target: its own geometry; every addressed byte lies inside its mapping.
        X86_PROFILE_BEGIN(target_start);
        int valid = redirect->Valid(redirect);
        X86_PROFILE_END(X86_PROFILE_TARGET, target_start);

        if (!valid || fb->Used > redirect->Bytes || last > redirect->Bytes - 4) return 0;

        base = redirect->Base;
    }
    else {
        mapped = owner->Authority.Region.Pages * 4096;
        start = fb->Base & 4095;
        if (last > mapped - 4 || start > mapped - 4 - last ||
            owner->Authority.Region.Virtual > ~0ULL - start - last) return 0;
        base = owner->Authority.Region.Virtual + start;
    }

    if (base & 3) return 0;

    X86_PROFILE_BEGIN(verify_start);
    int checked = verify_private(owner);
    X86_PROFILE_END(X86_PROFILE_VERIFY, verify_start);

    if (!checked) return 0;
    session->Owner = owner; session->Surface = *fb; session->Base = base;
    session->X = x; session->Y = y; session->Width = width; session->Height = height;
    return 1;
}

// ====== x86_fb_pixels_store ======

int x86_fb_pixels_store(const X86_FB_PIXEL_SESSION *session,
    BOOT_U32 x, BOOT_U32 y, BOOT_U32 color)
{
    BOOT_U64 offset;
    volatile BOOT_U32 *pixel;
    if (!session || (color & 0xff000000U) || x >= session->Surface.Width ||
        y >= session->Surface.Height || x < session->X || y < session->Y ||
        x - session->X >= session->Width || y - session->Y >= session->Height) return 0;
    offset = ((BOOT_U64)y * session->Surface.Stride + x) * 4;
    if (session->Surface.Used < 4 || offset > session->Surface.Used - 4 ||
        session->Base > ~0ULL - offset) return 0;
    pixel = x86_fb_pixel_view(session->Base + offset);
    if (!pixel) return 0;
    *pixel = color;
    return *pixel == color;
}

// ====== x86_fb_pixels_end ======

int x86_fb_pixels_end(const X86_FB_PIXEL_SESSION *session)
{
    if (!session || !x86_fb_authority(&session->Owner->Authority)) return 0;

    if (!redirect) return 1;

    X86_PROFILE_BEGIN(target_start);
    int valid = redirect->Valid(redirect);
    X86_PROFILE_END(X86_PROFILE_TARGET, target_start);
    return valid;
}

// ====== x86_fb_pixel ======

int x86_fb_pixel(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    unsigned char red, unsigned char green, unsigned char blue)
{
    X86_FB_PIXEL_SESSION session;
    BOOT_U32 color;
    if (!x86_fb_pixels_begin(owner, x, y, 1, 1, &session)) return 0;
    color = fb_color_word(session.Surface.Format, red, green, blue);
    return x86_fb_pixels_store(&session, x, y, color) && x86_fb_pixels_end(&session);
}
