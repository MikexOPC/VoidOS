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
    if (area > 1944 || !x86_fb_pixels_begin(owner, captured[0].X, captured[0].Y,
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
    if (!accepted || transaction->Accesses >= 2268 || x >= transaction->Pixel.Surface.Width ||
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

// ====== x86_fb_transaction_row ======

// UEFI2.10 12.9 GOP: aligned 32-bit RGB/BGR words in the already owned UC mapping.
int x86_fb_transaction_span(X86_FB_PIXEL_TRANSACTION *tx, BOOT_U32 rect,
    BOOT_U32 row, BOOT_U32 column, BOOT_U32 width, const BOOT_U32 *words, BOOT_U32 capacity)
{
    volatile BOOT_U32 *pixels[72];
    X86_FB_PIXEL_RECT region;
    BOOT_U64 bytes, address, offset;
    if (!tx || tx->Self != (BOOT_U64)tx || tx->Failed || !words || ((BOOT_U64)words & 3) ||
        !tx->Count || tx->Count > 3 || rect >= tx->Count) return 0;
    region = tx->Rects[rect];
    if (column >= region.Width || !width || width > region.Width-column ||
        region.X > ~0U-column) goto failed;
    region.X += column; region.Width = width;
    bytes = (BOOT_U64)region.Width * 4;
    address = (BOOT_U64)words;
    if (!region.Width || region.Width > 72 || row >= region.Height || capacity < region.Width ||
        address > ~0ULL - bytes || !tx->Pixel.Owner || tx->Pixel.Owner->Self != (BOOT_U64)tx->Pixel.Owner ||
        tx->Pixel.Owner->Ready != X86_FB_RENDERING || tx->Pixel.Owner->Published != 1 ||
        tx->Root != tx->Pixel.Owner->Authority.Root || tx->Accesses > 2268 - region.Width ||
        os_address_ranges_overlap(address,bytes,(BOOT_U64)tx,sizeof(*tx)) ||
        os_address_ranges_overlap(address,bytes,(BOOT_U64)tx->Pixel.Owner,sizeof(*tx->Pixel.Owner)) ||
        os_address_ranges_overlap(address,bytes,(BOOT_U64)tx->Pixel.Owner->Context,sizeof(*tx->Pixel.Owner->Context)) ||
        os_address_ranges_overlap(address,bytes,X86_FB_VIRTUAL,4321ULL*4096)) goto failed;
    for (BOOT_U32 x = 0; x < region.Width; ++x) {
        if (region.X > ~0U-x || region.Y > ~0U-row) goto failed;
        offset = fb_pixel_offset(tx->Pixel.Surface.Width,tx->Pixel.Surface.Height,tx->Pixel.Surface.Stride,
            tx->Pixel.Surface.Format,tx->Pixel.Surface.Used,region.X+x,region.Y+row);
        if (offset == ~0ULL || tx->Pixel.Base > ~0ULL-offset ||
            !(pixels[x] = x86_fb_pixel_view(tx->Pixel.Base+offset))) goto failed;
    }
    tx->Accesses += region.Width;
    tx->Writes += region.Width;
    for (BOOT_U32 x = 0; x < region.Width; ++x) *pixels[x] = words[x];
    for (BOOT_U32 x = 0; x < region.Width; ++x) if (*pixels[x] != words[x]) goto failed;
    return 1;
failed:
    tx->Failed = 1;
    return 0;
}

// ====== x86_fb_transaction_row ======

int x86_fb_transaction_row(X86_FB_PIXEL_TRANSACTION *tx, BOOT_U32 rect,
    BOOT_U32 row, const BOOT_U32 *words, BOOT_U32 capacity)
{
    if (!tx || rect >= 3 || rect >= tx->Count) return 0;
    return x86_fb_transaction_span(tx,rect,row,0,tx->Rects[rect].Width,words,capacity);
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
