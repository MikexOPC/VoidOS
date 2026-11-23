#ifndef OS_X86_PROCESS_GUI_H
#define OS_X86_PROCESS_GUI_H

#include "internal.h"
#include "kernel/drivers/video/display.h"
#include "kernel/gui/gui.h"

typedef struct {
    BOOT_U64  ShmId;
    BOOT_U64  UserVirt;
    BOOT_U32  Width;
    BOOT_U32  Height;
    BOOT_U32  Stride;
    BOOT_U32  Format;
} X86_DISPLAY_ACQUIRE_OUT;

_Static_assert(sizeof(X86_DISPLAY_ACQUIRE_OUT) == 32U, "X86_DISPLAY_ACQUIRE_OUT ABI size");

int x86_gui_init(void);
void x86_gui_bind_display(KERNEL_DISPLAY display);
void x86_gui_release_owned(uint64_t pid, X86_PROCESS_SLOT *slot);

BOOT_U64 x86_gui_sys_display_info(X86_PROCESS_CPU *cpu, BOOT_U64 info_ptr);
BOOT_U64 x86_gui_sys_display_acquire(X86_PROCESS_CPU *cpu, BOOT_U64 out_ptr);
BOOT_U64 x86_gui_sys_display_present(X86_PROCESS_CPU *cpu, BOOT_U64 rects_ptr, BOOT_U64 count);
BOOT_U64 x86_gui_sys_input_poll(X86_PROCESS_CPU *cpu, BOOT_U64 event_ptr);
BOOT_U64 x86_gui_sys_shm_create(X86_PROCESS_CPU *cpu, BOOT_U64 pages);
BOOT_U64 x86_gui_sys_shm_map(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id);
BOOT_U64 x86_gui_sys_shm_unmap(X86_PROCESS_CPU *cpu, BOOT_U64 shm_id);

#endif
