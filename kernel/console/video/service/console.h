#ifndef KERNEL_CONSOLE_SERVICE_H
#define KERNEL_CONSOLE_SERVICE_H
#include "../console.h"
#define KERNEL_CONSOLE_TEXT_LIMIT 256U
typedef struct {
    uintptr_t             Self;
    KERNEL_VIDEO_CONSOLE   *View;
    KERNEL_VIDEO_CONSOLE   Transcript;
    uint32_t              Line[KERNEL_CONSOLE_TEXT_LIMIT];
    uint32_t              Length, Busy, Closed, Failed;
} KERNEL_CONSOLE_SERVICE;
uint64_t kernel_console_utf8(uint32_t packed, uint32_t available, uint32_t controls);
int kernel_console_service_init(KERNEL_CONSOLE_SERVICE *owner, KERNEL_VIDEO_CONSOLE *view,
    uint32_t columns, uint32_t rows);
int kernel_console_service_edit(KERNEL_CONSOLE_SERVICE *owner,
    const char *data, uint32_t length, uint32_t capacity);
int kernel_console_service_write(KERNEL_CONSOLE_SERVICE *owner,
    const char *data, uint32_t length, uint32_t capacity);
int kernel_console_service_submit(KERNEL_CONSOLE_SERVICE *owner);
int kernel_console_service_render_begin(KERNEL_CONSOLE_SERVICE *owner);
int kernel_console_service_render_end(KERNEL_CONSOLE_SERVICE *owner, int success);
int kernel_console_service_pending(const KERNEL_CONSOLE_SERVICE *owner);
int kernel_console_service_close(KERNEL_CONSOLE_SERVICE *owner);
#endif
