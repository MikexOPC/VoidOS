#include "draw.h"
#include "../pixels/session.h"
typedef struct {
    BOOT_U64  Self, Root;
    X86_FB_MAPPING  *Owner;
    OS_FRAMEBUFFER  Surface;
    KERNEL_POINTER_IMAGE  Image;
    // Drawing target the saved background was read from (0 = GOP aperture, see pixels/session.h).
    const X86_FB_TARGET  *Target;
    BOOT_U32  Pixels[324], Valid, Failed;
    BOOT_U32  Staged[1944];
    BOOT_U32  Original[324];
    unsigned char  Dirty[1944];
} CURSOR_BACKUP;
static CURSOR_BACKUP backup;

// ====== same_surface ======

static int same_surface(const OS_FRAMEBUFFER *first, const OS_FRAMEBUFFER *second)
{
    return second && first->Base == second->Base && first->Width == second->Width &&
        first->Height == second->Height && first->Stride == second->Stride &&
        first->Format == second->Format && first->Used == second->Used && first->Capacity == second->Capacity;
}

// ====== cursor_rectangle ======

static X86_FB_PIXEL_RECT cursor_rectangle(const KERNEL_POINTER_IMAGE *image)
{
    X86_FB_PIXEL_RECT rect = {image->X, image->Y, 18, 18};
    return rect;
}

// ====== contains ======

static int contains(const X86_FB_PIXEL_RECT *rect, BOOT_U32 x, BOOT_U32 y)
{
    return x >= rect->X && y >= rect->Y && x-rect->X < rect->Width && y-rect->Y < rect->Height;
}

// ====== text_pixel ======

static BOOT_U32 text_pixel(const X86_FB_TEXT *text, const X86_FB_FONT *font,
    const BOOT_U32 *glyphs, BOOT_U32 caret, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U32 x, BOOT_U32 y)
{
    BOOT_U32 cell = (x-text->X)/X86_FB_CELL_WIDTH, row = y-text->Y;
    return x86_fb_text_color(cell == caret && row >= font->Height-2 ? ~0ULL :
        x86_fb_font_row(font,glyphs[cell],row), (x-text->X)%X86_FB_CELL_WIDTH, foreground, background);
}

// ====== x86_fb_cursor_text ======

int x86_fb_cursor_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *text,
    const X86_FB_FONT *font, const BOOT_U32 glyphs[X86_FB_TEXT_CELLS], BOOT_U32 count, BOOT_U32 caret,
    const KERNEL_POINTER_IMAGE *pointer)
{
    X86_FB_PIXEL_TRANSACTION transaction = {0};
    X86_FB_PIXEL_RECT rects[3], old = cursor_rectangle(&backup.Image), current;
    BOOT_U32 next[324], used = 0;
    X86_FB_PIXEL_RECT text_rect;
    BOOT_U32 rectangles = 0, foreground, background;
    OS_FRAMEBUFFER geometry;
    // V2c: the background under the cursor belongs to the target it was read from; after a target
    // change (GPU bring-up or GOP fallback) it is dropped and the full redraw repaints the old box.
    if (backup.Valid && backup.Target != x86_fb_target()) backup.Valid = 0;
    if (backup.Failed || !owner || (!x86_fb_target() && !owner->Context) || !text || !font || !glyphs || !pointer || pointer->Visible > 1 ||
        !count || count > 8 || (backup.Valid && (backup.Self != (BOOT_U64)&backup ||
        backup.Owner != owner || backup.Root != owner->Authority.Root ||
        !same_surface(&backup.Surface, x86_fb_surface_geometry(owner, &geometry))))) return 0;
    current = cursor_rectangle(pointer);
    text_rect = (X86_FB_PIXEL_RECT){text->X, text->Y, count * X86_FB_CELL_WIDTH, X86_FB_CELL_HEIGHT};
    if (backup.Valid) rects[rectangles++] = old;
    rects[rectangles++] = text_rect;
    if (pointer->Visible) rects[rectangles++] = current;
    if (!x86_fb_transaction_begin(owner, rects, rectangles, &transaction)) return 0;
    foreground = fb_color_word(transaction.Pixel.Surface.Format, (unsigned char)(text->Foreground >> 16),
        (unsigned char)(text->Foreground >> 8), (unsigned char)text->Foreground);
    background = fb_color_word(transaction.Pixel.Surface.Format, (unsigned char)(text->Background >> 16),
        (unsigned char)(text->Background >> 8), (unsigned char)text->Background);
    if (pointer->Visible) {
        for (BOOT_U32 y = 0; y < 18; ++y) for (BOOT_U32 x = 0; x < 18; ++x) {
            BOOT_U32 gx = current.X+x, gy = current.Y+y, *pixel = &next[y*18+x];
            if (!x86_fb_transaction_pixel(&transaction,gx,gy,pixel,0)) goto failed;
            backup.Original[y*18+x] = *pixel;
            if (backup.Valid && contains(&old,gx,gy)) *pixel = backup.Pixels[(gy-old.Y)*18+gx-old.X];
            if (contains(&text_rect,gx,gy)) *pixel = text_pixel(text,font,glyphs,caret,foreground,background,gx,gy);
        }
    }
    for (BOOT_U32 r = 0; r < rectangles; ++r) {
        const X86_FB_PIXEL_RECT *rect = &rects[r];
        for (BOOT_U32 y = 0; y < rect->Height; ++y) for (BOOT_U32 x = 0; x < rect->Width; ++x) {
            BOOT_U32 gx = rect->X+x, gy = rect->Y+y, color = 0;
            if (backup.Valid && contains(&old,gx,gy)) color = backup.Pixels[(gy-old.Y)*18+gx-old.X];
            if (contains(&text_rect,gx,gy)) color = text_pixel(text,font,glyphs,caret,foreground,background,gx,gy);
            if (pointer->Visible && contains(&current,gx,gy)) {
                BOOT_U32 sample = kernel_pointer_sample(gx-current.X,gy-current.Y);
                color = next[(gy-current.Y)*18+gx-current.X];
                if (sample) color = kernel_pointer_blend(sample,color & 0xffffff);
            }
            if (used >= 1944) goto failed;
            backup.Dirty[used] = 1;
            if (pointer->Visible && contains(&current,gx,gy))
                backup.Dirty[used] = color != backup.Original[(gy-current.Y)*18+gx-current.X];
            else if (backup.Valid && contains(&old,gx,gy)) {
                BOOT_U32 before = backup.Pixels[(gy-old.Y)*18+gx-old.X];
                BOOT_U32 mask = kernel_pointer_sample(gx-old.X,gy-old.Y);
                if (mask) before = kernel_pointer_blend(mask,before & 0xffffff);
                backup.Dirty[used] = color != before;
            }
            for (BOOT_U32 previous = 0; previous < r; ++previous)
                if (contains(&rects[previous],gx,gy)) backup.Dirty[used] = 0;
            backup.Staged[used++] = color;
        }
    }
    used = 0;
    for (BOOT_U32 r = 0; r < rectangles; ++r) for (BOOT_U32 y = 0; y < rects[r].Height; ++y) {
        BOOT_U32 x = 0;
        while (x < rects[r].Width) {
            BOOT_U32 start, length;
            while (x < rects[r].Width && !backup.Dirty[used+x]) ++x;
            start = x;
            while (x < rects[r].Width && backup.Dirty[used+x]) ++x;
            length = x-start;
            if (length && !x86_fb_transaction_span(&transaction,r,y,start,length,&backup.Staged[used+start],1944-used-start)) goto failed;
        }
        used += rects[r].Width;
    }
    if (!x86_fb_transaction_end(&transaction)) goto failed;
    backup.Self = (BOOT_U64)&backup; backup.Owner = owner; backup.Root = owner->Authority.Root;
    backup.Surface = *x86_fb_surface_geometry(owner, &geometry); backup.Image = *pointer; backup.Target = x86_fb_target();
    backup.Valid = pointer->Visible;
    if (pointer->Visible) for (BOOT_U32 index = 0; index < 324; ++index) backup.Pixels[index] = next[index];
    return 1;
failed:
    backup.Failed = 1;
    return 0;
}
