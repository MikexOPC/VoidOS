#ifndef OS_X86_FB_PIXEL_SESSION_H
#define OS_X86_FB_PIXEL_SESSION_H
#include "write.h"
// Z1-A: an alternative drawing target with the GOP geometry (width, height, stride and pixel
// format identical) but another mapping, e.g. the VirtIO surface. While one is registered every
// pixel session writes there instead of the GOP aperture; the GOP owner keeps all its checks.
typedef struct X86_FB_TARGET {
    BOOT_U64  Self, Base, Bytes;
    int  (*Valid)(const struct X86_FB_TARGET *target);
} X86_FB_TARGET;
int x86_fb_target_redirect(const X86_FB_TARGET *expected, const X86_FB_TARGET *next);
const X86_FB_TARGET *x86_fb_target(void);
typedef struct {
    X86_FB_MAPPING  *Owner;
    OS_FRAMEBUFFER  Surface;
    BOOT_U64  Base;
    BOOT_U32  X, Y, Width, Height;
} X86_FB_PIXEL_SESSION;
int x86_fb_pixels_begin(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, X86_FB_PIXEL_SESSION *session);
int x86_fb_pixels_store(const X86_FB_PIXEL_SESSION *session,
    BOOT_U32 x, BOOT_U32 y, BOOT_U32 color);
int x86_fb_pixels_end(const X86_FB_PIXEL_SESSION *session);
BOOT_U32 x86_fb_rect_valid(BOOT_U64 surface, BOOT_U64 origin, BOOT_U64 extent);
typedef struct {
    BOOT_U32  X, Y, Width, Height;
} X86_FB_PIXEL_RECT;
typedef struct {
    BOOT_U64  Self, Root;
    X86_FB_PIXEL_SESSION  Pixel;
    X86_FB_PIXEL_RECT  Rects[3];
    BOOT_U32  Count, Accesses, Writes, Failed;
} X86_FB_PIXEL_TRANSACTION;
int x86_fb_transaction_begin(X86_FB_MAPPING *owner, const X86_FB_PIXEL_RECT *rects,
    BOOT_U32 count, X86_FB_PIXEL_TRANSACTION *transaction);
int x86_fb_transaction_pixel(X86_FB_PIXEL_TRANSACTION *transaction,
    BOOT_U32 x, BOOT_U32 y, BOOT_U32 *word, int write);
int x86_fb_transaction_row(X86_FB_PIXEL_TRANSACTION *transaction,
    BOOT_U32 rect, BOOT_U32 row, const BOOT_U32 *words, BOOT_U32 capacity);
int x86_fb_transaction_span(X86_FB_PIXEL_TRANSACTION *transaction,
    BOOT_U32 rect, BOOT_U32 row, BOOT_U32 column, BOOT_U32 width,
    const BOOT_U32 *words, BOOT_U32 capacity);
int x86_fb_transaction_end(X86_FB_PIXEL_TRANSACTION *transaction);
int x86_fb_transaction_abort(X86_FB_PIXEL_TRANSACTION *transaction);
#endif
