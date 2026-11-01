#ifndef OS_X86_AP_BLOB_H
#define OS_X86_AP_BLOB_H
#include "../entry.h"

// ====== blob_software_abi ======

#define XB_PAGE 4096U
#define XB_GATE 104U
#define XB_ERROR 108U
#define XB_IVT 192U
#define XB_IDT32 320U
#define XB_IDT64 576U
#define XB_METADATA_END 1088U
_Static_assert(XB_GATE == sizeof(XS_ENTRY_RECORD) && XB_ERROR == XB_GATE + 4 &&
    XB_METADATA_END < XB_PAGE, "Low-page software ABI and stack room");

uint32_t xs_blob_layout(uint32_t bytes, uint32_t entry32, uint32_t entry64, uint32_t gdt,
    uint32_t nmi16, uint32_t fail16, uint32_t fail32, uint32_t fail64);
int xb_build(unsigned char *code, size_t code_capacity, unsigned char *data, size_t data_capacity,
    const unsigned char *template_bytes, size_t template_size, XS_ENTRY_RECORD record, uint32_t bits);
int xb_verify(const unsigned char *code, size_t code_capacity, const unsigned char *data, size_t data_capacity,
    const unsigned char *template_bytes, size_t template_size, XS_ENTRY_RECORD record, uint32_t bits);
#endif
