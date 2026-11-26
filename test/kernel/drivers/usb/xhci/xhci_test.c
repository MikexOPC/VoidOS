#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/usb/xhci/xhci.h"

typedef struct {
    uint32_t  Count;
    uint32_t  Codes[16];
    uint32_t  Scalars[16];
    uint32_t  Flags[16];
} KEY_LOG;

typedef struct {
    uint32_t  Count;
    int32_t   Dx;
    int32_t   Dy;
    uint32_t  Buttons;
} MOUSE_LOG;

// ====== on_key_cb ======

static void on_key_cb(void *ctx, uint32_t code, uint32_t scalar, uint32_t flags)
{
    KEY_LOG *log = (KEY_LOG *)ctx;
    if (log->Count < 16U) {
        log->Codes[log->Count] = code;
        log->Scalars[log->Count] = scalar;
        log->Flags[log->Count] = flags;
        ++log->Count;
    }
}

// ====== on_mouse_cb ======

static void on_mouse_cb(void *ctx, int32_t dx, int32_t dy, uint32_t buttons)
{
    MOUSE_LOG *log = (MOUSE_LOG *)ctx;
    ++log->Count;
    log->Dx = dx;
    log->Dy = dy;
    log->Buttons = buttons;
}

// ====== test_parse_hid_descriptors ======

static void test_parse_hid_descriptors(void)
{
    // Realistic USB Boot Keyboard Configuration Descriptor (34 bytes).
    static const uint8_t kbd_cfg[] = {
        9, 2, 34, 0, 1, 1, 0, 0xA0, 50,             // Config (bConfigurationValue=1)
        9, 4, 0, 0, 1, 3, 1, 1, 0,                   // Interface 0: HID(3), Boot(1), Keyboard(1)
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,      // HID descriptor
        7, 5, 0x81, 0x03, 8, 0, 10                   // EP 0x81 IN, Interrupt, MPS=8, Interval=10
    };
    XHCI_HID_INFO info = {0};
    assert(kernel_xhci_parse_hid_config(kbd_cfg, sizeof(kbd_cfg), &info) == 1);
    assert(info.ConfigValue == 1U);
    assert(info.InterfaceNumber == 0U);
    assert(info.Protocol == USB_PROTO_KEYBOARD);
    assert(info.EndpointAddr == 0x81U);
    assert(info.MaxPacketSize == 8U);
    assert(info.Interval == 10U);

    // Realistic USB Boot Mouse Configuration Descriptor.
    static const uint8_t mouse_cfg[] = {
        9, 2, 34, 0, 1, 2, 0, 0xA0, 50,
        9, 4, 1, 0, 1, 3, 1, 2, 0,                   // Interface 1: HID(3), Boot(1), Mouse(2)
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 52, 0,
        7, 5, 0x82, 0x03, 4, 0, 8
    };
    memset(&info, 0, sizeof(info));
    assert(kernel_xhci_parse_hid_config(mouse_cfg, sizeof(mouse_cfg), &info) == 1);
    assert(info.ConfigValue == 2U);
    assert(info.InterfaceNumber == 1U);
    assert(info.Protocol == USB_PROTO_MOUSE);
    assert(info.EndpointAddr == 0x82U);
    assert(info.MaxPacketSize == 4U);

    // Truncated / non-HID descriptors must be rejected cleanly.
    static const uint8_t mass_storage[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 8, 6, 0x50, 0,
        7, 5, 0x81, 0x02, 64, 0, 0
    };
    assert(kernel_xhci_parse_hid_config(mass_storage, sizeof(mass_storage), &info) == 0);
    assert(kernel_xhci_parse_hid_config(kbd_cfg, 5U, &info) == 0);
}

// ====== test_hid_keyboard_decode ======

static void test_hid_keyboard_decode(void)
{
    uint8_t prev[8] = {0};
    KEY_LOG log = {0};

    // Press 'a' (usage 0x04), unshifted.
    uint8_t r1[8] = {0x00, 0, 0x04, 0, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r1, on_key_cb, &log);
    assert(log.Count == 1U);
    assert(log.Codes[0] == 0x04U);
    assert(log.Scalars[0] == 'a');
    assert(log.Flags[0] == 0U);

    // Hold 'a', press Shift + 'b' (usage 0x05) -> only 'B' press emitted.
    uint8_t r2[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r2, on_key_cb, &log);
    assert(log.Count == 2U);
    assert(log.Codes[1] == 0x05U);
    assert(log.Scalars[1] == 'B');
    assert((log.Flags[1] & XHCI_KEY_MOD_SHIFT) != 0U);

    // Rollover report (0x01) must be ignored without corrupting prev.
    uint8_t rollover[8] = {0x02, 0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    kernel_xhci_decode_hid_kbd(prev, rollover, on_key_cb, &log);
    assert(log.Count == 2U);

    // Release both 'a' and 'b', press Enter (0x28).
    uint8_t r3[8] = {0x00, 0, 0x28, 0, 0, 0, 0, 0};
    kernel_xhci_decode_hid_kbd(prev, r3, on_key_cb, &log);
    assert(log.Count == 5U);
    assert(log.Codes[2] == (XHCI_KEY_EVENT_RELEASE | 0x04U));
    assert(log.Codes[3] == (XHCI_KEY_EVENT_RELEASE | 0x05U));
    assert(log.Codes[4] == 0x28U);
    assert(log.Scalars[4] == 0x110003U);
}

// ====== test_hid_mouse_decode ======

static void test_hid_mouse_decode(void)
{
    MOUSE_LOG log = {0};
    uint8_t m1[3] = {0x01, 0xFB, 0x0A}; // Left button, dx=-5, dy=+10
    kernel_xhci_decode_hid_mouse(m1, sizeof(m1), on_mouse_cb, &log);
    assert(log.Count == 1U);
    assert(log.Buttons == 1U);
    assert(log.Dx == -5);
    assert(log.Dy == 10);

    // Short report (< 3 bytes) ignored safely.
    uint8_t bad[2] = {0x01, 0x02};
    kernel_xhci_decode_hid_mouse(bad, sizeof(bad), on_mouse_cb, &log);
    assert(log.Count == 1U);
}

// ====== main ======

int main(void)
{
    test_parse_hid_descriptors();
    test_hid_keyboard_decode();
    test_hid_mouse_decode();
    puts("xhci_host: PASS");
    return 0;
}
