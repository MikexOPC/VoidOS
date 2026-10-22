#include "pointer_test.h"
#include "arch/x86/kernel/drivers/video/framebuffer/font/font.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#define CHECK(value) do { if (!(value)) return __LINE__ + 1000; } while (0)

// ====== run_pointer_drawing_tests ======

int run_pointer_drawing_tests(POINTER_DRAW_SETUP setup, X86_FB_MAPPING *owner, BOOT_U32 *words)
{
    const X86_FB_FONT *font = x86_fb_console_font();
    const unsigned char letters[] = "AAAA";
    X86_FB_TEXT text = {letters,4,sizeof(letters),0,0,0x112233,0x445566};
    KERNEL_POINTER_IMAGE pointer = {2,4,1};
    {
        X86_FB_PIXEL_TRANSACTION tx = {0}, copied;
        X86_FB_PIXEL_RECT rects[2] = {{0,0,1,1},{31,31,1,1}};
        BOOT_U32 word = 0x123456, original;
        CHECK(setup(32,32,36,1));
        original = words[0];
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(owner->Ready == X86_FB_RENDERING && !x86_fb_unbind(owner));
        {
            X86_FB_PIXEL_TRANSACTION nested = {0};
            CHECK(!x86_fb_transaction_begin(owner,rects,2,&nested));
        }
        copied = tx;
        CHECK(!x86_fb_transaction_pixel(&copied,0,0,&word,1));
        CHECK(words[0] == original);
        CHECK(!x86_fb_transaction_pixel(&tx,15,15,&word,1));
        CHECK(!x86_fb_transaction_end(&tx));
        CHECK(x86_fb_transaction_abort(&tx));
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(x86_fb_transaction_row(&tx,0,0,&word,1));
        CHECK(tx.Accesses == 1 && tx.Writes == 1 && words[0] == word);
        CHECK(!x86_fb_transaction_abort(&tx));
        CHECK(x86_fb_transaction_end(&tx));
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(!x86_fb_transaction_row(&tx,0,1,&word,1) && !tx.Writes);
        CHECK(x86_fb_transaction_abort(&tx));
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(!x86_fb_transaction_row(&tx,0,0,&word,0) && !tx.Writes);
        CHECK(x86_fb_transaction_abort(&tx));
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(!x86_fb_transaction_row(&tx,0,0,&tx.Count,1) && !tx.Writes);
        CHECK(x86_fb_transaction_abort(&tx));
        CHECK(words[15*36+15] == 0xa5a5a5a5);
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(x86_fb_transaction_pixel(&tx,0,0,&word,1));
        CHECK(!x86_fb_transaction_abort(&tx) && !x86_fb_unbind(owner));
        word = 0;
        CHECK(x86_fb_transaction_pixel(&tx,0,0,&word,0) && word == 0x123456);
        CHECK(x86_fb_transaction_end(&tx));
        CHECK(!x86_fb_transaction_pixel(&tx,0,0,&word,0));
        CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
        for (BOOT_U32 index = 0; index < 2268; ++index)
            CHECK(x86_fb_transaction_pixel(&tx,0,0,&word,0));
        CHECK(!x86_fb_transaction_pixel(&tx,0,0,&word,0) && !x86_fb_transaction_end(&tx));
        CHECK(x86_fb_transaction_abort(&tx));
        rects[0].Width = 4;
        {
            BOOT_U32 colors[2] = {0xabcdef,0x123456}, first = words[0];
            CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
            CHECK(!x86_fb_transaction_span(&tx,0,0,3,2,colors,2) && !tx.Writes);
            CHECK(x86_fb_transaction_abort(&tx));
            CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
            CHECK(!x86_fb_transaction_span(&tx,0,0,1,2,colors,1) && !tx.Writes);
            CHECK(x86_fb_transaction_abort(&tx));
            CHECK(x86_fb_transaction_begin(owner,rects,2,&tx));
            CHECK(x86_fb_transaction_span(&tx,0,0,1,2,colors,2));
            CHECK(words[0] == first && words[1] == colors[0] && words[2] == colors[1] && words[3] == 0xa5a5a5a5);
            CHECK(tx.Accesses == 2 && tx.Writes == 2 && x86_fb_transaction_end(&tx));
        }
        rects[1].X = 32;
        CHECK(!x86_fb_transaction_begin(owner,rects,2,&tx));
        CHECK(x86_fb_unbind(owner));
    }
    for (BOOT_U32 format = 0; format < 2; ++format) {
        const BOOT_U32 w = font->Width, h = font->Height, stride = 4 * w + 4;
        CHECK(w == 9 && h == 18);
        CHECK(setup(4 * w,2 * h,stride,format));
        for (BOOT_U32 phase = 0; phase < 2; ++phase) {
            BOOT_U32 visible = phase == 0;
            pointer.Visible = visible;
            for (BOOT_U32 row = 0; row < 2; ++row) {
                text.Y = row * h;
                CHECK(x86_fb_text_pointer(owner,&text,font,1,&pointer));
            }
            for (BOOT_U32 y = 0; y < 2 * h; ++y) for (BOOT_U32 x = 0; x < 4 * w; ++x) {
                BOOT_U32 sample = visible && x >= 2 && y >= 4 ? kernel_pointer_sample(x-2,y-4) : 0;
                BOOT_U32 rgb = x86_fb_text_color(y%h >= h-2 && x >= w && x < 2*w ? ~0ULL : x86_fb_font_row(font,65-32,y%h),x%w,0x112233,0x445566);
                if (sample) {
                    BOOT_U32 expected = 0, black = sample & 31, white = sample >> 5;
                    for (BOOT_U32 shift = 0; shift < 24; shift += 8)
                        expected |= ((((rgb >> shift) & 255) * (16-black-white) + 255*white + 8) / 16) << shift;
                    rgb = expected;
                }
                BOOT_U32 color = format ? rgb : ((rgb & 255) << 16) | (rgb & 0xff00) | (rgb >> 16);
                CHECK(words[y*stride+x] == color);
                CHECK(words[y*stride+4*w] == 0xa5a5a5a5);
            }
        }
        pointer.Visible = 1; pointer.X = ~0U;
        CHECK(!x86_fb_text_pointer(owner,&text,font,1,&pointer));
        pointer.X = 2; pointer.Visible = 2;
        CHECK(!x86_fb_text_pointer(owner,&text,font,1,&pointer));
        pointer.Visible = 1;
        for (BOOT_U32 step = 0; step < 32; ++step) {
            pointer.X = step % 19; pointer.Y = (step * 7) % 19;
            text.Y = 0;
            CHECK(x86_fb_text_pointer(owner,&text,font,1,&pointer));
            for (BOOT_U32 y = 0; y < 2*h; ++y) for (BOOT_U32 x = 0; x < 4*w; ++x) {
                BOOT_U32 rgb = x86_fb_text_color(y%h >= h-2 && x >= w && x < 2*w ? ~0ULL :
                    x86_fb_font_row(font,65-32,y%h),x%w,0x112233,0x445566);
                BOOT_U32 mask = x >= pointer.X && y >= pointer.Y ? kernel_pointer_sample(x-pointer.X,y-pointer.Y) : 0;
                if (mask) rgb = kernel_pointer_blend(mask,rgb);
                CHECK(words[y*stride+x] == (format ? rgb : ((rgb&255)<<16)|(rgb&0xff00)|(rgb>>16)));
            }
        }
        pointer.Visible = 0;
        CHECK(x86_fb_text_pointer(owner,&text,font,1,&pointer));
        pointer.Visible = 1; pointer.X = 2; pointer.Y = 4;
        CHECK(x86_fb_unbind(owner));
    }
    return 0;
}
