#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H
#include <stdint.h>

// G6 O3: what the console needs from a presenting display (today the VirtIO GPU session; later a
// real GPU or the GUI compositor). Ops 0 means drawing goes straight to the framebuffer (GOP).
// Every operation returns 0 on failure; Busy and Redraw return 1 when true.
typedef struct {
    int  (*Check)(void *self);
    int  (*Poll)(void *self);
    int  (*Busy)(void *self);
    int  (*Redraw)(void *self);
    int  (*Rect)(void *self, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
} KERNEL_DISPLAY_OPS;

typedef struct {
    void                       *Self;
    const KERNEL_DISPLAY_OPS   *Ops;
} KERNEL_DISPLAY;
#endif
