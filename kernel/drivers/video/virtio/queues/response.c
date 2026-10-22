#include "queue.h"
#include "lib/memory/address_ranges.h"

// ====== word_private ======

static uint32_t word_private(const unsigned char *data,uint32_t offset)
{
    return (uint32_t)data[offset] | (uint32_t)data[offset+1]<<8 |
        (uint32_t)data[offset+2]<<16 | (uint32_t)data[offset+3]<<24;
}

// ====== kernel_vgpu_queue_response ======

// VirtIO1.2 5.7.6.8: RESP_OK_DISPLAY_INFO0x1101, header24 +16 entries24.
int kernel_vgpu_queue_response_selected(const unsigned char *data,uint32_t bytes,uint64_t expected,uint32_t *width,uint32_t *height,uint32_t *scanout)
{
    uint32_t first_width=0, first_height=0, selected=0;
    if (!data || bytes!=408 || !expected || !width || !height || !scanout ||
        os_address_ranges_overlap((uintptr_t)scanout,4,(uintptr_t)width,4) ||
        os_address_ranges_overlap((uintptr_t)scanout,4,(uintptr_t)height,4) ||
        os_address_ranges_overlap((uintptr_t)data,bytes,(uintptr_t)scanout,4) ||
        os_address_ranges_overlap((uintptr_t)width,4,(uintptr_t)height,4) ||
        os_address_ranges_overlap((uintptr_t)data,bytes,(uintptr_t)width,4) ||
        os_address_ranges_overlap((uintptr_t)data,bytes,(uintptr_t)height,4) ||
        word_private(data,0)!=0x1101 || word_private(data,4)!=1 ||
        ((uint64_t)word_private(data,8)|((uint64_t)word_private(data,12)<<32))!=expected ||
        word_private(data,16) || word_private(data,20)) return 0;
    for (uint32_t slot=0; slot<16; ++slot) {
        uint32_t at=24+slot*24;
        uint32_t x=word_private(data,at), y=word_private(data,at+4);
        uint32_t w=word_private(data,at+8), h=word_private(data,at+12);
        uint32_t enabled=word_private(data,at+16), flags=word_private(data,at+20);
        if (!kernel_vgpu_queue_display(x,y,w,h,enabled,flags)) return 0;
        if (enabled && !first_width) { first_width=w; first_height=h; selected=slot; }
    }
    if (!first_width) return 0;
    *width=first_width; *height=first_height; *scanout=selected;
    return 1;
}

// ====== kernel_vgpu_queue_response ======

int kernel_vgpu_queue_response(const unsigned char *data,uint32_t bytes,uint64_t expected,uint32_t *width,uint32_t *height)
{
    uint32_t scanout;
    return kernel_vgpu_queue_response_selected(data,bytes,expected,width,height,&scanout);
}
