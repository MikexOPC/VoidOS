#include "read.h"

// ====== kernel_vgpu_config_read ======

// VirtIO1.2 4.1.4.9: type5 pci_cfg_data (+16..19) forwards BAR reads.
// Read standard header and capability metadata only, never that window.
int kernel_vgpu_config_read(KERNEL_VGPU_CONFIG_READ read, void *context, unsigned char output[256])
{
    unsigned char draft[256]={0}, occupied[256]={0};
    uint32_t cursor, count=0;
    if (!read || !context || !output || (uintptr_t)output>UINTPTR_MAX-256 ||
        !read(context,0,64,draft)) return 0;
    if ((draft[0x0e]&127) || !(draft[6]&16)) return 0;
    cursor=draft[0x34];
    while (cursor) {
        uint32_t length=2, metadata=2;
        if (++count>48 || cursor<64 || cursor>252 || (cursor&3) ||
            occupied[cursor] || occupied[cursor+1] || !read(context,cursor,2,draft+cursor)
        )
        return 0;

        if (draft[cursor]==9) {
            if (
                occupied[cursor+2] || occupied[cursor+3] || !read(context,cursor+2,2,draft+cursor+2)
            )
            return 0;
            length=draft[cursor+2];
            if (length<4 || length>256-cursor) return 0;
            if (draft[cursor+3]>=1 && draft[cursor+3]<=4) {
                metadata=draft[cursor+3]==2?20U:16U;
                if (length<metadata) return 0;
            }
        }
        for (uint32_t byte=0; byte<length; ++byte) {
            if (occupied[cursor+byte]) return 0;
            occupied[cursor+byte]=1;
        }
        if (metadata>4 && !read(context,cursor+4,metadata-4,draft+cursor+4)) return 0;
        cursor=draft[cursor+1];
    }
    for (uint32_t byte=0; byte<256; ++byte) output[byte]=draft[byte];
    return 1;
}
