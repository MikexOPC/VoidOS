#include "arch/x86/kernel/descriptors/segments.h"
static X86_KERNEL_SEGMENTS segments;
static BOOT_U32 activation_calls, register_reads;

// ====== mock attivazione non usato dal costruttore puro ===== //

void x86_kernel_segments_activate(const X86_GDT_POINTER *pointer)
{
    (void)pointer;
    ++activation_calls;
}

// ====== nessun SGDT privilegiato nel processo host ===== //

void x86_kernel_segments_read(X86_GDT_POINTER *pointer)
{
    (void)pointer;
    ++register_reads;
}

// ====== costruttore da stato vergine e tutti i byte significativi verificati ===== //

static int corruption_tests(void)
{
    BOOT_U32 byte, bit;
    unsigned char *data = (unsigned char *)&segments;
    if (!x86_kernel_segments_prepare(&segments) || !x86_kernel_segments_verify(&segments, 0)) return 1;
    if (segments.Gdt.Entries[1] != 0x00af9b000000ffffULL || segments.Gdt.Entries[2] != 0x00cf93000000ffffULL ||
        segments.Pointer.Bytes[0] != 39 || segments.Descriptor.Bytes[5] != 0x89) return 2;
    for (byte = 0; byte < sizeof(segments); ++byte) {
        // Padding tra GDT e TSS e padding finale non vengono usati dalla CPU.
        if ((byte >= 40 && byte < offsetof(X86_KERNEL_SEGMENTS, Task)) ||
            byte >= offsetof(X86_KERNEL_SEGMENTS, Pointer) + 10) continue;
        for (bit = 0; bit < 8; ++bit) {
            data[byte] ^= (unsigned char)(1U << bit);
            if (x86_kernel_segments_verify(&segments, 0)) return 3;
            data[byte] ^= (unsigned char)(1U << bit);
        }
    }
    segments.Descriptor.Bytes[5] = 0x8b;
    if (!x86_kernel_segments_verify(&segments, 1) || x86_kernel_segments_verify(&segments, 0) ||
        x86_kernel_segments_prepare(&segments)) return 4;
    if (x86_kernel_segments_verify(&segments, 2) || x86_kernel_segments_verify(0, 0) ||
        x86_kernel_segments_prepare(0)) return 5;
    return 0;
}

// ====== stato busy con le stesse verifiche su ogni byte hardware ===== //

static int busy_corruption_tests(void)
{
    BOOT_U32 byte, bit;
    unsigned char *data = (unsigned char *)&segments;
    segments.Descriptor.Bytes[5] = 0x8b;
    for (byte = 0; byte < sizeof(segments); ++byte) {
        if ((byte >= 40 && byte < offsetof(X86_KERNEL_SEGMENTS, Task)) ||
            byte >= offsetof(X86_KERNEL_SEGMENTS, Pointer) + 10) continue;
        for (bit = 0; bit < 8; ++bit) {
            data[byte] ^= (unsigned char)(1U << bit);
            if (x86_kernel_segments_verify(&segments, 1)) return 19;
            data[byte] ^= (unsigned char)(1U << bit);
        }
    }
    return x86_kernel_segments_verify(&segments, 1) ? 0 : 20;
}

// ====== memoria vergine senza dipendenze da memset del runtime ===== //

static void clear_segments(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(segments); ++byte) {
        ((volatile unsigned char *)&segments)[byte] = 0;
    }
}

// ====== lettura little endian indipendente dalle funzioni di produzione ===== //

static BOOT_U64 read_le(const unsigned char *bytes, BOOT_U32 count)
{
    BOOT_U32 byte;
    BOOT_U64 value = 0;
    for (byte = 0; byte < count; ++byte) {
        value |= (BOOT_U64)bytes[byte] << (byte * 8);
    }
    return value;
}

// ====== byte TSS derivati dal layout hardware e da estremi indipendenti ===== //

static unsigned char expected_task(BOOT_U32 byte)
{
    const BOOT_U32 offsets[] = {4, 36, 44, 52};
    const BOOT_U64 values[] = {
        0x700000011000ULL, 0x700000045000ULL,
        0x70000004a000ULL, 0x70000004f000ULL
    };
    BOOT_U32 field;
    for (field = 0; field < 4; ++field) {
        if (byte >= offsets[field] && byte < offsets[field] + 8) {
            return (unsigned char)(values[field] >> ((byte - offsets[field]) * 8));
        }
    }
    return byte == 102 ? 104 : 0;
}

// ====== descriptor base a 64 bit limite 103 e bitmap oltre la TSS ===== //

static int encoding_tests(void)
{
    BOOT_U64 base;
    BOOT_U32 byte;
    clear_segments();
    if (!x86_kernel_segments_prepare(&segments)) return 6;
    for (byte = 0; byte < 104; ++byte) {
        if (segments.Task.Bytes[byte] != expected_task(byte)) return 7;
    }
    base = read_le(segments.Descriptor.Bytes + 2, 3) |
        ((BOOT_U64)segments.Descriptor.Bytes[7] << 24) |
        (read_le(segments.Descriptor.Bytes + 8, 4) << 32);
    if (base != (BOOT_U64)&segments.Task ||
        read_le(segments.Descriptor.Bytes, 2) != 103 ||
        segments.Descriptor.Bytes[6] != 0 ||
        read_le(segments.Descriptor.Bytes + 12, 4) != 0 ||
        read_le(segments.Pointer.Bytes, 2) != 39 ||
        read_le(segments.Pointer.Bytes + 2, 8) != (BOOT_U64)&segments.Gdt ||
        read_le(segments.Task.Bytes + 102, 2) != sizeof(segments.Task)) return 8;
    return 0;
}

// ====== nessun byte modificato quando lo storage contiene stato precedente ===== //

static int fresh_storage_tests(void)
{
    BOOT_U32 byte, check;
    for (byte = 0; byte < sizeof(segments); ++byte) {
        clear_segments();
        ((volatile unsigned char *)&segments)[byte] = 0xa5;
        if (x86_kernel_segments_prepare(&segments)) return 9;
        for (check = 0; check < sizeof(segments); ++check) {
            unsigned char expected = check == byte ? 0xa5 : 0;
            if (((const unsigned char *)&segments)[check] != expected) return 10;
        }
    }
    clear_segments();
    return x86_kernel_segments_prepare(&segments) ? 0 : 11;
}

// ====== solo available e busy previsti senza mascherare tipi sconosciuti ===== //

static int task_type_tests(void)
{
    BOOT_U32 type;
    clear_segments();
    if (!x86_kernel_segments_prepare(&segments)) return 12;
    for (type = 0; type < 256; ++type) {
        segments.Descriptor.Bytes[5] = (unsigned char)type;
        if (x86_kernel_segments_verify(&segments, 0) != (type == 0x89) ||
            x86_kernel_segments_verify(&segments, 1) != (type == 0x8b)) return 13;
    }
    segments.Descriptor.Bytes[5] = 0x89;
    return x86_kernel_segments_verify(&segments, 0) ? 0 : 14;
}

// ====== puntatori respinti prima di leggere storage non raggiungibile ===== //

static int argument_tests(void)
{
    X86_KERNEL_SEGMENTS copy;
    const BOOT_U64 invalid[] = {
        0, 16, (BOOT_U64)&segments + 1,
        X86_BOOT_LOW_ADDRESS_LIMIT - 16,
        X86_KERNEL_IST_GUARD, X86_KERNEL_IST_GUARD + 4096,
        ~0ULL
    };
    BOOT_U32 index, byte;
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        X86_KERNEL_SEGMENTS *owner = (X86_KERNEL_SEGMENTS *)invalid[index];
        if (x86_kernel_segments_prepare(owner) || x86_kernel_segments_verify(owner, 0)) return 15;
    }
    clear_segments();
    if (!x86_kernel_segments_prepare(&segments)) return 16;
    for (byte = 0; byte < sizeof(copy); ++byte) {
        ((unsigned char *)&copy)[byte] = ((const unsigned char *)&segments)[byte];
    }
    if (x86_kernel_segments_verify(&copy, 0) || x86_kernel_segments_prepare(&copy)) return 17;
    return 0;
}

// ====== purezza dei costruttori senza LGDT LTR o letture del registro ===== //

__declspec(dllexport) int run_segment_tests(void)
{
    int result;
    clear_segments();
    if ((result = corruption_tests()) != 0) return result;
    if ((result = encoding_tests()) != 0) return result;
    if ((result = fresh_storage_tests()) != 0) return result;
    if ((result = task_type_tests()) != 0) return result;
    if ((result = argument_tests()) != 0) return result;
    if ((result = busy_corruption_tests()) != 0) return result;
    return activation_calls || register_reads ? 18 : 0;
}
