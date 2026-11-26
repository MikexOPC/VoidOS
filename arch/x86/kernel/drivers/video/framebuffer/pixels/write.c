#include "session.h"
#include "../console/profile.h"
#include "arch/x86/kernel/exceptions/terminal.h"

static const X86_FB_TARGET *redirect;

// G5 V2: the pixel path keeps no full scan. The paging inventory is scanned in full when the cache
// is empty (reset at the session boundaries) or when owner or CPU paging state change. A moved
// paging or allocator generation is a change made through the kernel APIs, which verify what they
// build (V4): it is accepted without a scan. Anything that bypasses the APIs is the job of the idle
// audit (x86_fb_audit, at most once per second).
static X86_FB_CPU verified_cpu;
static BOOT_U64 verified_generation;
static BOOT_U32 verified_pool, verified_ready;
static const X86_FB_MAPPING *verified_owner;
static BOOT_U64 verify_full, verify_skipped, verify_accepted;

// ====== x86_fb_verify_reset ======

void x86_fb_verify_reset(void) { verified_ready = 0; }

// ====== x86_fb_verify_counts ======

void x86_fb_verify_counts(BOOT_U64 *full, BOOT_U64 *skipped, BOOT_U64 *accepted)
{
    if (full) *full = verify_full;
    if (skipped) *skipped = verify_skipped;
    if (accepted) *accepted = verify_accepted;
}

// ====== x86_fb_audit ======

// One full scan of the paging inventory against the owner's snapshot (the idle audit, the first
// session after a reset, or an owner/CPU change).
int x86_fb_audit(X86_FB_MAPPING *owner)
{
    if (redirect && owner && !owner->Published)
        return redirect->Valid(redirect);
    return owner && owner->Context && owner->Window && owner->Window->Pool && owner->Lapic && owner->Inventory &&
        x86_paging_inventory_verify_video(owner->Context, owner->Window->Pool, owner->Lapic, &owner->Authority,
            x86_fb_authority, owner->Inventory);
}

// ====== verify_private ======

static int verify_private(X86_FB_MAPPING *owner)
{
    X86_FB_CPU cpu;

    if (!x86_fb_cpu(&cpu)) return 0;

    BOOT_U64 generation = x86_paging_generation();
    BOOT_U64 pool;
    if (!kernel_physical_generation_read(owner->Window->Pool, &pool)) return 0;
    int same = verified_ready && verified_owner == owner && cpu.Root == verified_cpu.Root &&
        cpu.Pat == verified_cpu.Pat && cpu.PhysicalBits == verified_cpu.PhysicalBits;

    if (same && verified_generation == generation && verified_pool == pool) { ++verify_skipped; return 1; }

    if (same) {
        ++verify_accepted;
    }
    else {
        verified_ready = 0;
        if (!x86_fb_audit(owner)) return 0;
        ++verify_full;
        verified_cpu = cpu;
        verified_owner = owner;
        verified_ready = 1;
    }

    verified_generation = x86_paging_generation();
    if (!kernel_physical_generation_read(owner->Window->Pool, &pool)) { verified_ready = 0; return 0; }
    verified_pool = (BOOT_U32)pool;
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
    if (!scratch) return 0;

    if (!redirect) return (owner && owner->Context) ? &owner->Context->Info.Framebuffer : 0;

    *scratch = (OS_FRAMEBUFFER){0};
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
    int redirect_only = (redirect != 0 && owner && !owner->Published);
    if (!session || !owner || ((BOOT_U64)owner & 7) || !x86_exception_environment()) return 0;
    if (!redirect_only && (owner->Self != (BOOT_U64)owner || !owner->Context ||
        owner->Ready != 2 || owner->Published != 1 || !owner->Window ||
        !owner->Window->Pool || !owner->Lapic || !owner->Inventory ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        owner->Allocated != owner->Authority.Region.TableCount ||
        owner->Pinned != (1U << owner->Allocated) - 1 ||
        !x86_page_window_idle_valid(owner->Window))) return 0;
    fb = x86_fb_surface_geometry(owner, &geometry);
    if (!fb || !x86_fb_rect_valid(((BOOT_U64)fb->Height << 32) | fb->Width,
        ((BOOT_U64)y << 32) | x, ((BOOT_U64)height << 32) | width)) return 0;
    first = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used, x, y);
    last = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used,
        x + width - 1, y + height - 1);
    if (first == ~0ULL || last == ~0ULL || (!redirect_only && (!owner->Authority.Region.Pages ||
        owner->Authority.Region.Pages > 4321))) return 0;
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

    if (!redirect_only) {
        X86_PROFILE_BEGIN(verify_start);
        int checked = verify_private(owner);
        X86_PROFILE_END(X86_PROFILE_VERIFY, verify_start);

        if (!checked) return 0;
    }
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
    if (!session || (!(redirect && !session->Owner->Published) && !x86_fb_authority(&session->Owner->Authority))) return 0;

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
