#include "session.h"
#include "lib/memory/address_ranges.h"

// ====== x86_fb_transaction_begin ======

int x86_fb_transaction_begin(X86_FB_MAPPING *owner, const X86_FB_PIXEL_RECT *rects,
    BOOT_U32 count, X86_FB_PIXEL_TRANSACTION *transaction)
{
    X86_FB_PIXEL_RECT captured[3];
    BOOT_U64 area = 0;
    if (!transaction || ((BOOT_U64)transaction & 7) || !rects || ((BOOT_U64)rects & 3) || !count || count > 3 ||
        !owner || ((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner || !owner->Context) return 0;
    if (os_address_ranges_overlap((BOOT_U64)transaction,sizeof(*transaction),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)transaction,sizeof(*transaction),(BOOT_U64)owner->Context,sizeof(*owner->Context))) return 0;
    for (BOOT_U32 index = 0; index < count; ++index) {
        const OS_FRAMEBUFFER *fb = &owner->Context->Info.Framebuffer;
        BOOT_U64 last, pages = owner->Authority.Region.Pages, start = fb->Base & 4095;
        captured[index] = rects[index];
        if (!x86_fb_rect_valid(((BOOT_U64)fb->Height << 32) | fb->Width,
            ((BOOT_U64)captured[index].Y << 32) | captured[index].X,
            ((BOOT_U64)captured[index].Height << 32) | captured[index].Width)) return 0;
        last = fb_pixel_offset(fb->Width,fb->Height,fb->Stride,fb->Format,fb->Used,
            captured[index].X + captured[index].Width - 1,captured[index].Y + captured[index].Height - 1);
        if (!pages || pages > 4321 || last == ~0ULL || last > pages * 4096 - 4 ||
            start > pages * 4096 - 4 - last) return 0;
        area += (BOOT_U64)captured[index].Width * captured[index].Height;
    }
    if (area > 2248 || !x86_fb_pixels_begin(owner, captured[0].X, captured[0].Y,
        captured[0].Width, captured[0].Height, &transaction->Pixel)) return 0;
    transaction->Self = (BOOT_U64)transaction;
    transaction->Root = owner->Authority.Root;
    transaction->Count = count; transaction->Accesses = transaction->Writes = transaction->Failed = 0;
    for (BOOT_U32 index = 0; index < count; ++index) transaction->Rects[index] = captured[index];
    owner->Ready = X86_FB_RENDERING;
    return 1;
}

// ====== x86_fb_transaction_pixel ======

// UEFI2.10 §12.9 GOP: RGB/BGR reserved8-bit pixels, stride in pixels.
// Address is validated published kernel mapping + (y*stride+x)*4; reads
// capture the owned background and stores preserve captured reserved bits.
int x86_fb_transaction_pixel(X86_FB_PIXEL_TRANSACTION *transaction,
    BOOT_U32 x, BOOT_U32 y, BOOT_U32 *word, int write)
{
    BOOT_U32 accepted = 0;
    BOOT_U64 offset;
    volatile BOOT_U32 *pixel;
    if (!transaction || transaction->Self != (BOOT_U64)transaction || transaction->Failed ||
        !word || ((BOOT_U64)word & 3) || (write != 0 && write != 1) || !transaction->Count || transaction->Count > 3) return 0;
    if (!transaction->Pixel.Owner || transaction->Pixel.Owner->Self != (BOOT_U64)transaction->Pixel.Owner ||
        transaction->Pixel.Owner->Ready != X86_FB_RENDERING || transaction->Pixel.Owner->Published != 1 ||
        transaction->Root != transaction->Pixel.Owner->Authority.Root) goto failed;
    for (BOOT_U32 index = 0; index < transaction->Count; ++index) {
        const X86_FB_PIXEL_RECT *rect = &transaction->Rects[index];
        accepted |= x >= rect->X && y >= rect->Y && x - rect->X < rect->Width && y - rect->Y < rect->Height;
    }
    if (!accepted || transaction->Accesses >= 2572 || x >= transaction->Pixel.Surface.Width ||
        y >= transaction->Pixel.Surface.Height) goto failed;
    offset = fb_pixel_offset(transaction->Pixel.Surface.Width, transaction->Pixel.Surface.Height,
        transaction->Pixel.Surface.Stride, transaction->Pixel.Surface.Format,
        transaction->Pixel.Surface.Used, x, y);
    if (offset == ~0ULL || transaction->Pixel.Base > ~0ULL - offset ||
        !(pixel = x86_fb_pixel_view(transaction->Pixel.Base + offset))) goto failed;
    ++transaction->Accesses;
    if (write) { ++transaction->Writes; *pixel = *word; if (*pixel != *word) goto failed; }
    else *word = *pixel;
    return 1;
failed:
    transaction->Failed = 1;
    return 0;
}

// ====== x86_fb_transaction_end ======

int x86_fb_transaction_end(X86_FB_PIXEL_TRANSACTION *transaction)
{
    if (!transaction || transaction->Self != (BOOT_U64)transaction || transaction->Failed ||
        !transaction->Pixel.Owner || transaction->Pixel.Owner->Ready != X86_FB_RENDERING ||
        !transaction->Accesses || transaction->Root != transaction->Pixel.Owner->Authority.Root ||
        !x86_fb_pixels_end(&transaction->Pixel)) return 0;
    transaction->Pixel.Owner->Ready = 2;
    transaction->Self = 0;
    return 1;
}

// ====== x86_fb_transaction_abort ======

int x86_fb_transaction_abort(X86_FB_PIXEL_TRANSACTION *transaction)
{
    if (!transaction || transaction->Self != (BOOT_U64)transaction || transaction->Writes ||
        !transaction->Pixel.Owner || transaction->Pixel.Owner->Ready != X86_FB_RENDERING ||
        transaction->Root != transaction->Pixel.Owner->Authority.Root ||
        !x86_fb_pixels_end(&transaction->Pixel)) return 0;
    transaction->Pixel.Owner->Ready = 2;
    transaction->Self = 0;
    return 1;
}
