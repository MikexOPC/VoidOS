#include "arch/x86/kernel/cpu/percpu/startup/blob/blob.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char template_bytes[XB_PAGE], code[XB_PAGE], data[XB_PAGE];

// ====== record_private ======

static XS_ENTRY_RECORD record_private(uint64_t physical_code, uint64_t physical_data) {
    XS_ENTRY_INPUT input = {1, 0, 48, 0, 520, physical_code, physical_data, 0x10000, 0x20000,
        0xffffffff80010000ULL, 0xffffffff80100000ULL, 0x0007040600070406ULL};
    XS_ENTRY_RECORD record;
    assert(xs_entry_build(&record, input));
    return record;
}

// ====== save_private ======

static void save_private(const char *prefix, const char *suffix, const unsigned char *bytes) {
    char name[1024];
    int length = snprintf(name, sizeof(name), "%s%s", prefix, suffix);
    assert(length > 0 && (size_t)length < sizeof(name));
    FILE *file = NULL;
    int opened = fopen_s(&file, name, "wb");
    assert(!opened && file);
    size_t written = fwrite(bytes, 1, XB_PAGE, file);
    int closed = fclose(file);
    assert(written == XB_PAGE && !closed);
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *file = NULL;
    int opened = fopen_s(&file, argv[1], "rb");
    assert(!opened && file);
    size_t read = fread(template_bytes, 1, XB_PAGE, file);
    int extra = fgetc(file), closed = fclose(file);
    assert(read == XB_PAGE && extra == EOF && !closed);
    XS_ENTRY_RECORD r = record_private(0x8000, 0x9000);
    memset(code, 0xa5, sizeof(code)); memset(data, 0xa5, sizeof(data));
    assert(!xb_build(code, XB_PAGE - 1, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(!xb_build(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE - 1, r, 48));
    assert(!xb_build(code, XB_PAGE, code + 1, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(!xb_build(template_bytes, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(!xb_build((unsigned char *)(UINTPTR_MAX - 8), XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    r.Reserved = 1;
    assert(!xb_build(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    r.Reserved = 0;
    for (unsigned int i = 0; i < XB_PAGE; ++i) assert(code[i] == 0xa5 && data[i] == 0xa5);
    template_bytes[30] = 1;
    assert(!xb_build(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    template_bytes[30] = 0;
    assert(xb_build(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(xb_verify(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(!memcmp(data, &r, sizeof(r)));
    for (unsigned int i = 0; i < XB_PAGE; ++i) {
        code[i] ^= 1;
        assert(!xb_verify(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
        code[i] ^= 1;
        data[i] ^= 1;
        assert(!xb_verify(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
        data[i] ^= 1;
    }
    save_private(argv[2], "-8000-code.bin", code);
    save_private(argv[2], "-8000-data.bin", data);
    r = record_private(0xa000, 0xb000);
    assert(xb_build(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    assert(xb_verify(code, XB_PAGE, data, XB_PAGE, template_bytes, XB_PAGE, r, 48));
    save_private(argv[2], "-a000-code.bin", code);
    save_private(argv[2], "-a000-data.bin", data);
    puts("PASS: blob package, aliases/capacity, 8192 byte corruptions, two relocations; local buffers only");
    return 0;
}
