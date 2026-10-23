#include "session.h"
#include "../console/profile.h"
#include "arch/x86/kernel/exceptions/terminal.h"

static const X86_FB_TARGET *redirect;

// ====== x86_fb_target_redirect ======

int x86_fb_target_redirect(const X86_FB_TARGET *expected, const X86_FB_TARGET *next)
{
    if (redirect != expected ||
        (next && (expected || next->Self != (BOOT_U64)next || !next->Valid || !next->Base || (next->Base & 4095) ||
            !next->Bytes || next->Base > ~0ULL - next->Bytes || !next->Valid(next)))
    )
    return 0;

    redirect = next;
    return 1;
}

// ====== x86_fb_target ======

const X86_FB_TARGET *x86_fb_target(void) { return redirect; }

// ====== x86_fb_pixels_begin ======

int x86_fb_pixels_begin(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, X86_FB_PIXEL_SESSION *session)
{
    const OS_FRAMEBUFFER *fb;
    BOOT_U64 first, last, mapped, start, base;
    if (!session || !owner || ((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner ||
        owner->Ready != 2 || owner->Published != 1 || !owner->Context || !owner->Window ||
        !owner->Window->Pool || !owner->Lapic || !owner->Inventory ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        owner->Allocated != owner->Authority.Region.TableCount ||
        owner->Pinned != (1U << owner->Allocated) - 1 || !x86_exception_environment() ||
        !x86_page_window_idle_valid(owner->Window)) return 0;
    fb = &owner->Context->Info.Framebuffer;
    if (!x86_fb_rect_valid(((BOOT_U64)fb->Height << 32) | fb->Width,
        ((BOOT_U64)y << 32) | x, ((BOOT_U64)height << 32) | width)) return 0;
    first = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used, x, y);
    last = fb_pixel_offset(fb->Width, fb->Height, fb->Stride, fb->Format, fb->Used,
        x + width - 1, y + height - 1);
    if (first == ~0ULL || last == ~0ULL || !owner->Authority.Region.Pages ||
        owner->Authority.Region.Pages > 4321) return 0;
    mapped = owner->Authority.Region.Pages * 4096;
    start = fb->Base & 4095;
    if (last > mapped - 4 || start > mapped - 4 - last ||
        owner->Authority.Region.Virtual > ~0ULL - start - last) return 0;
    base = owner->Authority.Region.Virtual + start;

    // Redirected target: same geometry by contract, so only the base changes; it must still hold
    // every byte the GOP layout addresses.
    if (redirect) {
        X86_PROFILE_BEGIN(target_start);
        int valid = redirect->Valid(redirect);
        X86_PROFILE_END(X86_PROFILE_TARGET, target_start);

        if (!valid || fb->Used > redirect->Bytes || last > redirect->Bytes - 4) return 0;

        base = redirect->Base;
    }

    if (base & 3) return 0;

    X86_PROFILE_BEGIN(verify_start);
    int verified = x86_paging_inventory_verify_video(owner->Context, owner->Window->Pool,
        owner->Lapic, &owner->Authority, x86_fb_authority, owner->Inventory);
    X86_PROFILE_END(X86_PROFILE_VERIFY, verify_start);

    if (!verified) return 0;
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
