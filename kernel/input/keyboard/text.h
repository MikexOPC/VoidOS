#ifndef KERNEL_PS2_TEXT_H
#define KERNEL_PS2_TEXT_H
#include <stdint.h>
#define KERNEL_PS2_TEXT_INVALID UINT32_MAX
#define KERNEL_PS2_TEXT_BACKSPACE 0x110001U
#define KERNEL_PS2_TEXT_TAB 0x110002U
#define KERNEL_PS2_TEXT_ENTER 0x110003U

// ====== task_owned_text_contract ======

/* State0 resets held modifiers and Caps Lock. High32 result=next state;
   low32=Unicode scalar,0=no text, editing action above Unicode range,
   or INVALID (next state0). Feed complete decoder events only.
   Integer ABI: no pointer, allocation, global or IRQ work.
   LOSS/malformed transport requires both decoder AND text state reset.
   Layout Italian00000410 subset: main block/ISO key, right Alt=AltGr;
   Ctrl/left Alt suppress text, no Ctrl+Alt alias, keypad/compose/LEDs absent. */
uint64_t kernel_ps2_text(uint32_t state, uint32_t event);
/* Unicode3.9 UTF-8: high32=length1..4, low32=packed bytes little-endian.
   Invalid scalar (surrogate or >10FFFF) returns0. Caller owns destination. */
uint64_t kernel_ps2_utf8(uint32_t scalar);
#endif
