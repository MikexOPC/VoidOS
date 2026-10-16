#ifndef TEST_FRAMEBUFFER_POINTER_H
#define TEST_FRAMEBUFFER_POINTER_H
#include "arch/x86/kernel/drivers/video/framebuffer/drawing/draw.h"
typedef int (*POINTER_DRAW_SETUP)(BOOT_U32 width, BOOT_U32 height, BOOT_U32 stride, BOOT_U32 format);
int run_pointer_drawing_tests(POINTER_DRAW_SETUP setup, X86_FB_MAPPING *owner, BOOT_U32 *words);
#endif
