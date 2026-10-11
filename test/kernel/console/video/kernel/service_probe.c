#include "probe.h"
#include "kernel/console/video/service/console.h"
#include "kernel/diagnostics/log.h"
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;

// ====== kernel_console_service_probe ======

int kernel_console_service_probe(void)
{
    static const char italian[] = "c\xc3\xa8 \xe2\x82\xac";
    static const char response[] = "OK\n";
    for (uint32_t cycle = 0; cycle < 100; ++cycle) {
        if (!kernel_console_service_init(&service, &view, 16, 4) ||
            !kernel_console_service_edit(&service, italian, sizeof(italian) - 1, sizeof(italian)) ||
            !kernel_console_service_write(&service, response, sizeof(response) - 1, sizeof(response)) ||
            service.Length != 4 || view.Cells[0] != 'O' || view.Cells[16] != '>' ||
            view.Cells[19] != 0xe8 || view.Cells[21] != 0x20ac ||
            !kernel_console_service_render_begin(&service) ||
            kernel_console_service_edit(&service, italian, 7, 7) ||
            !kernel_console_service_render_end(&service, 1) ||
            !kernel_console_service_submit(&service) || service.Length ||
            service.Transcript.Cells[19] != 0xe8 || view.Cells[32] != '>' ||
            !kernel_console_service_close(&service) ||
            kernel_console_service_write(&service, response, 3, 3)) return 0;
    }
    return kernel_log(0, "KERNEL_CONSOLE_SERVICE_RAM_OK");
}
