#include "platform.h"
#include <intrin.h>
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/framebuffer/font/font.h"
#ifdef KERNEL_PANIC_RUNTIME
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#endif
#define COM1 0x3f8U
// National Semiconductor NS16550D §8.4: 16-byte TX FIFO; LSR bit 5 (THRE=0x20) empty allows 16-byte burst.
#define SERIAL_FIFO_BURST 16U
#define EARLY_LOG_CAP 4096U
static int serial_ready;
static int serial_panic_mode;
static uint64_t serial_smp_lock;
static unsigned char serial_tx_buf[SERIAL_FIFO_BURST];
static unsigned int serial_tx_len;
static char early_log_buf[EARLY_LOG_CAP];
static unsigned int early_log_write;
static unsigned int early_log_count;
static int early_log_esc;

// ====== early_log_record_private ======

static void early_log_record_private(char value)
{
    if (value == '\x1b') {
        early_log_esc = 1;
        return;
    }
    if (early_log_esc) {
        if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'))
            early_log_esc = 0;
        return;
    }
    if (value == '\r')
        return;
    early_log_buf[early_log_write] = value;
    early_log_write = (early_log_write + 1U) % EARLY_LOG_CAP;
    if (early_log_count < EARLY_LOG_CAP)
        ++early_log_count;
}

// ====== gop_pack_color_private ======

static uint32_t gop_pack_color_private(BOOT_U32 format, uint8_t r, uint8_t g, uint8_t b)
{
    if (format == 0U)
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}

// ====== gop_draw_glyph_private ======

static void gop_draw_glyph_private(
    volatile uint32_t *fb,
    BOOT_U32 stride,
    BOOT_U32 width,
    BOOT_U32 height,
    const X86_FB_FONT *font,
    BOOT_U32 px,
    BOOT_U32 py,
    char ch,
    uint32_t fg,
    uint32_t bg
)
{
    if (!font || !font->Glyphs || px + X86_FB_CELL_WIDTH > width || py + X86_FB_CELL_HEIGHT > height)
        return;
    const unsigned short *rows = font->Missing;
    BOOT_U32 scalar = (unsigned char)ch;
    for (BOOT_U32 i = 0; i < font->Count; ++i) {
        if (font->Glyphs[i].Scalar == scalar) {
            rows = font->Glyphs[i].Rows;
            break;
        }
    }
    for (BOOT_U32 r = 0; r < X86_FB_CELL_HEIGHT; ++r) {
        unsigned short bits = rows[r];
        volatile uint32_t *dst = fb + (size_t)(py + r) * stride + px;
        for (BOOT_U32 c = 0; c < X86_FB_CELL_WIDTH; ++c) {
            unsigned short mask = (unsigned short)(1U << (X86_FB_CELL_WIDTH - 1U - c));
            dst[c] = (bits & mask) ? fg : bg;
        }
    }
}

// ====== gop_render_log_private ======

static void gop_render_log_private(const char *banner, int is_panic)
{
    BOOT_U64 base = 0;
    BOOT_U32 width = 0, height = 0, stride = 0, format = 1U;
    if (!x86_fb_kernel_surface_info(&base, &width, &height, &stride, &format) || !base)
        return;
    const X86_FB_FONT *font = x86_fb_console_font();
    if (!font || width < 64U || height < 64U)
        return;

    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)base;
    uint32_t bg = is_panic ? gop_pack_color_private(format, 24, 10, 14) : gop_pack_color_private(format, 12, 16, 22);
    uint32_t hdr_bg = is_panic ? gop_pack_color_private(format, 150, 24, 34) : gop_pack_color_private(format, 28, 52, 84);
    uint32_t fg = gop_pack_color_private(format, 235, 240, 245);
    uint32_t hdr_fg = gop_pack_color_private(format, 255, 255, 255);

    for (BOOT_U32 y = 0; y < height; ++y) {
        volatile uint32_t *row = fb + (size_t)y * stride;
        uint32_t fill = y < 32U ? hdr_bg : bg;
        for (BOOT_U32 x = 0; x < width; ++x)
            row[x] = fill;
    }

    BOOT_U32 cols = (width > 24U) ? (width - 24U) / X86_FB_CELL_WIDTH : 1U;
    BOOT_U32 max_rows = (height > 44U) ? (height - 44U) / X86_FB_CELL_HEIGHT : 1U;
    if (banner) {
        BOOT_U32 cx = 12U;
        for (BOOT_U32 i = 0; banner[i] && i < cols; ++i) {
            gop_draw_glyph_private(fb, stride, width, height, font, cx, 7U, banner[i], hdr_fg, hdr_bg);
            cx += X86_FB_CELL_WIDTH;
        }
    }

    unsigned int count = early_log_count;
    unsigned int start = (early_log_write + EARLY_LOG_CAP - count) % EARLY_LOG_CAP;
    unsigned int total_lines = 1U;
    for (unsigned int i = 0; i < count; ++i) {
        if (early_log_buf[(start + i) % EARLY_LOG_CAP] == '\n')
            ++total_lines;
    }
    unsigned int skip = total_lines > max_rows ? (total_lines - max_rows) : 0U;
    unsigned int line = 0U;
    BOOT_U32 row_idx = 0U;
    BOOT_U32 col_idx = 0U;
    for (unsigned int i = 0; i < count && row_idx < max_rows; ++i) {
        char ch = early_log_buf[(start + i) % EARLY_LOG_CAP];
        if (ch == '\n') {
            ++line;
            if (line > skip) {
                ++row_idx;
                col_idx = 0U;
            }
            continue;
        }
        if (line < skip)
            continue;
        if (col_idx < cols && ch >= 0x20 && ch < 0x7F) {
            gop_draw_glyph_private(
                fb, stride, width, height, font,
                12U + col_idx * X86_FB_CELL_WIDTH,
                40U + row_idx * X86_FB_CELL_HEIGHT,
                ch, fg, bg
            );
            ++col_idx;
        }
    }
}

// ====== serial_lock_acquire_private ======

static int serial_lock_acquire_private(uint64_t *saved_if_out)
{
    *saved_if_out = ks_irq_save();
    if (serial_panic_mode)
        return 0;
    for (unsigned int spin = 0; spin < 200000U; ++spin) {
        if (ks_atomic_compare(&serial_smp_lock, 0ULL, 1ULL))
            return 1;
        _mm_pause();
    }
    return 0;
}

// ====== serial_lock_release_private ======

static void serial_lock_release_private(int held, uint64_t saved_if)
{
    if (held)
        ks_atomic_store_release(&serial_smp_lock, 0ULL);
    ks_irq_restore(saved_if);
}

// ====== serial_flush_locked_private ======

static int serial_flush_locked_private(void)
{
    if (!serial_tx_len)
        return 1;
    for (unsigned int attempt = 0; attempt < 100000U; ++attempt) {
        unsigned char status = x86_read_port(COM1 + 5);
        if (status == 255)
            break;
        if (status & 0x20U) {
            for (unsigned int i = 0; i < serial_tx_len; ++i)
                x86_write_port(COM1, serial_tx_buf[i]);
            serial_tx_len = 0U;
            return 1;
        }
    }
    serial_tx_len = 0U;
    serial_ready = 0;
    return 0;
}

// ====== UART COM1 del profilo QEMU con interrupt disabilitati ====== //

int x86_serial_initialize(void)
{
    unsigned char scratch;
    serial_ready = 0;
    serial_panic_mode = 0;
    serial_tx_len = 0;
    ks_atomic_store_release(&serial_smp_lock, 0ULL);
    if (x86_read_port(COM1 + 5) == 255) return 0;
    scratch = x86_read_port(COM1 + 7);
    x86_write_port(COM1 + 7, 0x5a);
    if (x86_read_port(COM1 + 7) != 0x5a) { x86_write_port(COM1 + 7, scratch); return 0; }
    x86_write_port(COM1 + 7, scratch);
    x86_write_port(COM1 + 3, 0);
    x86_write_port(COM1 + 1, 0);
    x86_write_port(COM1 + 3, 0x80);
    x86_write_port(COM1, 1);
    x86_write_port(COM1 + 1, 0);
    x86_write_port(COM1 + 3, 3);
    x86_write_port(COM1 + 2, 7);
    x86_write_port(COM1 + 4, 3);
    if (x86_read_port(COM1 + 3) != 3) return 0;
    serial_ready = 1;
    return 1;
}

// ====== attesa limitata invece di blocco infinito sul trasmettitore ====== //

int x86_serial_character(char value)
{
    uint64_t saved_if = 0;
    int held = serial_lock_acquire_private(&saved_if);
    early_log_record_private(value);
    int ok = 1;
    if (serial_ready) {
        if (serial_panic_mode) {
            if (serial_tx_len)
                (void)serial_flush_locked_private();
            ok = 0;
            for (unsigned int attempt = 0; attempt < 100000U; ++attempt) {
                unsigned char status = x86_read_port(COM1 + 5);
                if (status == 255) break;
                if (status & 0x20U) { x86_write_port(COM1, (unsigned char)value); ok = 1; break; }
            }
            if (!ok) serial_ready = 0;
        } else {
            serial_tx_buf[serial_tx_len++] = (unsigned char)value;
            if (serial_tx_len >= SERIAL_FIFO_BURST || value == '\n')
                ok = serial_flush_locked_private();
        }
    }
    serial_lock_release_private(held, saved_if);
    return ok;
}

// ====== x86_serial_present ======

int x86_serial_present(void)
{
    return serial_ready != 0;
}

// ====== x86_serial_flush_gop_early ======

void x86_serial_flush_gop_early(void)
{
    if (!serial_ready)
        gop_render_log_private("VoidOS early boot log (no COM1)", 0);
}

// ====== kernel_log_character ======

int kernel_log_character(char value) {
    return x86_serial_character(value);
}

// ====== kernel_log_panic_prepare ======

void kernel_log_panic_prepare(const char *message) {
    serial_panic_mode = 1;
    if (serial_ready && serial_tx_len)
        (void)serial_flush_locked_private();
    char banner[96];
    const char *prefix = "VOIDOS KERNEL PANIC: ";
    unsigned int pos = 0U;
    for (unsigned int i = 0; prefix[i] && pos + 1U < sizeof(banner); ++i)
        banner[pos++] = prefix[i];
    if (message) {
        for (unsigned int i = 0; message[i] && pos + 1U < sizeof(banner); ++i)
            banner[pos++] = message[i];
    }
    banner[pos] = '\0';
    gop_render_log_private(banner, 1);
#ifdef KERNEL_PANIC_RUNTIME
    if (ks_atomic_load_acquire(&xp_panic_armed)) xp_panic_voluntary((BOOT_U64)message);
#else
    (void)message;
#endif
}

// ====== kernel_log_stop ======

__declspec(noreturn) void kernel_log_stop(BOOT_U64 marker) {
    x86_kernel_stop(marker);
}
