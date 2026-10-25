#ifndef OS_X86_FB_PIXEL_SESSION_H
#define OS_X86_FB_PIXEL_SESSION_H
#include "write.h"
// Z1-A: an alternative drawing target with its own mapping and geometry (width, height and
// stride in pixels; pixel format 1, UEFI PixelBlueGreenRedReserved8BitPerColor), e.g. the VirtIO
// surface. While one is registered every pixel session draws there with that geometry; the GOP
// owner keeps all its checks.
typedef struct X86_FB_TARGET {
    BOOT_U64  Self, Base, Bytes;
    BOOT_U32  Width, Height, Stride, Reserved;
    int  (*Valid)(const struct X86_FB_TARGET *target);
} X86_FB_TARGET;
int x86_fb_target_redirect(const X86_FB_TARGET *expected, const X86_FB_TARGET *next);
// H1-A: forget the verified snapshot (console session boundaries); counters of full and skipped scans.
void x86_fb_verify_reset(void);
void x86_fb_verify_counts(BOOT_U64 *full, BOOT_U64 *skipped, BOOT_U64 *accepted);
// G5: full scan of the paging inventory for the owner (idle audit).
int x86_fb_audit(X86_FB_MAPPING *owner);
const X86_FB_TARGET *x86_fb_target(void);
// Geometry the console draws with: the GOP descriptor, or one built from the registered target
// (format 1, Used = Stride * Height * 4) in scratch.
const OS_FRAMEBUFFER *x86_fb_surface_geometry(const X86_FB_MAPPING *owner, OS_FRAMEBUFFER *scratch);
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
