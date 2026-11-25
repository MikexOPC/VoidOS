#include "platform.h"
#include <intrin.h>
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"
#ifdef KERNEL_PANIC_RUNTIME
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#endif
#define COM1 0x3f8U
// National Semiconductor NS16550D §8.4: 16-byte TX FIFO; LSR bit 5 (THRE=0x20) empty allows 16-byte burst.
#define SERIAL_FIFO_BURST 16U
static int serial_ready;
static int serial_panic_mode;
static uint64_t serial_smp_lock;
static unsigned char serial_tx_buf[SERIAL_FIFO_BURST];
static unsigned int serial_tx_len;

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
    if (!serial_ready) return 0;
    uint64_t saved_if = 0;
    int held = serial_lock_acquire_private(&saved_if);
    int ok = 1;
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
    serial_lock_release_private(held, saved_if);
    return ok;
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
