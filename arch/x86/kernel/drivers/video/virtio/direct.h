#ifndef OS_X86_VGPU_DIRECT_H
#define OS_X86_VGPU_DIRECT_H

#include <stdint.h>
#include "arch/x86/kernel/timer/lapic/timer.h"

int x86_vgpu_direct_start(const X86_PM_CLOCK *clock);
int x86_vgpu_direct_active(void);
int x86_vgpu_direct_has_cursor(void);
int x86_vgpu_direct_bind_backbuffer(uint64_t phys_base, uint32_t width, uint32_t height);
void x86_vgpu_direct_unbind_backbuffer(void);
int x86_vgpu_direct_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void x86_vgpu_direct_cursor_set_pos(int32_t x, int32_t y);
void x86_vgpu_direct_cursor_move_delta(int32_t dx, int32_t dy);
void x86_vgpu_direct_cursor_hide(void);

#endif
