#include "draw.h"
#include "../pixels/session.h"
typedef struct {
    BOOT_U64  Self, Root;
    X86_FB_MAPPING  *Owner;
    OS_FRAMEBUFFER  Surface;
    KERNEL_POINTER_IMAGE  Image;
    BOOT_U32  Pixels[324], Valid, Failed;
} CURSOR_BACKUP;
static CURSOR_BACKUP backup;

// ====== same_surface ======

static int same_surface(const OS_FRAMEBUFFER *first, const OS_FRAMEBUFFER *second)
{
    return first->Base == second->Base && first->Width == second->Width &&
        first->Height == second->Height && first->Stride == second->Stride &&
        first->Format == second->Format && first->Used == second->Used && first->Capacity == second->Capacity;
}

// ====== cursor_rectangle ======

static X86_FB_PIXEL_RECT cursor_rectangle(const KERNEL_POINTER_IMAGE *image)
{
    X86_FB_PIXEL_RECT rect = {image->X, image->Y, 18, 18};
    return rect;
}

// ====== x86_fb_cursor_text ======

int x86_fb_cursor_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *text,
    const unsigned char rows[X86_FB_TEXT_CELLS][16], BOOT_U32 count,
    const KERNEL_POINTER_IMAGE *pointer)
{
    X86_FB_PIXEL_TRANSACTION transaction = {0};
    X86_FB_PIXEL_RECT rects[3];
    BOOT_U32 rectangles = 0, foreground, background;
    if (backup.Failed || !owner || !owner->Context || !text || !rows || !pointer || pointer->Visible > 1 ||
        !count || count > 8 || (backup.Valid && (backup.Self != (BOOT_U64)&backup ||
        backup.Owner != owner || backup.Root != owner->Authority.Root ||
        !same_surface(&backup.Surface, &owner->Context->Info.Framebuffer)))) return 0;
    if (backup.Valid) rects[rectangles++] = cursor_rectangle(&backup.Image);
    rects[rectangles++] = (X86_FB_PIXEL_RECT){text->X, text->Y, count * 8, 16};
    if (pointer->Visible) rects[rectangles++] = cursor_rectangle(pointer);
    if (!x86_fb_transaction_begin(owner, rects, rectangles, &transaction)) return 0;
    if (backup.Valid) {
        for (BOOT_U32 y = 0; y < 18; ++y) for (BOOT_U32 x = 0; x < 18; ++x)
            if (!x86_fb_transaction_pixel(&transaction, backup.Image.X + x, backup.Image.Y + y,
                &backup.Pixels[y * 18 + x], 1)) goto failed;
        backup.Valid = 0;
    }
    foreground = fb_color_word(transaction.Pixel.Surface.Format, (unsigned char)(text->Foreground >> 16),
        (unsigned char)(text->Foreground >> 8), (unsigned char)text->Foreground);
    background = fb_color_word(transaction.Pixel.Surface.Format, (unsigned char)(text->Background >> 16),
        (unsigned char)(text->Background >> 8), (unsigned char)text->Background);
    for (BOOT_U32 cell = 0; cell < count; ++cell) for (BOOT_U32 y = 0; y < 16; ++y)
        for (BOOT_U32 x = 0; x < 8; ++x) {
            BOOT_U32 color = rows[cell][y] & (0x80U >> x) ? foreground : background;
            if (!x86_fb_transaction_pixel(&transaction, text->X + cell * 8 + x, text->Y + y, &color, 1)) goto failed;
        }
    if (pointer->Visible) {
        for (BOOT_U32 y = 0; y < 18; ++y) for (BOOT_U32 x = 0; x < 18; ++x) {
            BOOT_U32 *pixel = &backup.Pixels[y * 18 + x];
            BOOT_U32 sample = kernel_pointer_sample(x, y), color;
            if (!x86_fb_transaction_pixel(&transaction, pointer->X + x, pointer->Y + y, pixel, 0)) goto failed;
            color = kernel_pointer_blend(sample, *pixel & 0xffffff);
            if (sample && !x86_fb_transaction_pixel(&transaction, pointer->X + x, pointer->Y + y, &color, 1)) goto failed;
        }
    }
    if (!x86_fb_transaction_end(&transaction)) goto failed;
    backup.Self = (BOOT_U64)&backup; backup.Owner = owner; backup.Root = owner->Authority.Root;
    backup.Surface = owner->Context->Info.Framebuffer; backup.Image = *pointer;
    backup.Valid = pointer->Visible;
    return 1;
failed:
    backup.Failed = 1;
    return 0;
}
