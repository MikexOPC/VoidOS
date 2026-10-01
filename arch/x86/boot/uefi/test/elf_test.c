#include "elf/validate.h"

static UINT8 fixture[512];
static ELF_LOAD_PLAN plan;

// ====== costruzione fixture ELF senza struct sovrapposte ====== //

static void put16(UINTN offset, UINT16 value)
{
    fixture[offset] = (UINT8)value;
    fixture[offset + 1] = (UINT8)(value >> 8);
}

static void put32(UINTN offset, UINT32 value)
{
    put16(offset, (UINT16)value);
    put16(offset + 2, (UINT16)(value >> 16));
}

static void put64(UINTN offset, UINT64 value)
{
    put32(offset, (UINT32)value);
    put32(offset + 4, (UINT32)(value >> 32));
}

static void prepare(void)
{
    UINTN i;
    for (i = 0; i < sizeof(fixture); ++i)
        fixture[i] = 0;
    fixture[0] = 0x7f;
    fixture[1] = 'E';
    fixture[2] = 'L';
    fixture[3] = 'F';
    fixture[4] = 2;
    fixture[5] = 1;
    fixture[6] = 1;
    put16(16, 2);
    put16(18, 62);
    put32(20, 1);
    put64(24, 0x100100);
    put64(32, 64);
    put16(52, 64);
    put16(54, 56);
    put16(56, 2);
    put32(64, 1);
    put32(68, 5);
    put64(72, 256);
    put64(80, 0x100100);
    put64(96, 16);
    put64(104, 64);
    put64(112, 256);
    put32(120, 1);
    put32(124, 6);
    put64(128, 384);
    put64(136, 0x200180);
    put64(152, 8);
    put64(160, 64);
    put64(168, 128);
}

// ====== fixture condivisa con i test di lettura ====== //

void make_kernel_fixture(UINT8 *buffer)
{
    UINTN i;
    prepare();
    for (i = 0; i < sizeof(fixture); ++i)
        buffer[i] = fixture[i];
}

// ====== tabella di mutazioni con esito atteso ====== //

typedef struct {
    UINTN Offset;
    UINTN Width;
    UINT64 Value;
    ELF_VALIDATION_RESULT Expected;
} INVALID_CASE;

static const INVALID_CASE invalid_cases[] = {
    {0, 1, 0, ELF_BAD_HEADER},
    {4, 1, 1, ELF_UNSUPPORTED_PROFILE},
    {5, 1, 2, ELF_UNSUPPORTED_PROFILE},
    {6, 1, 0, ELF_UNSUPPORTED_PROFILE},
    {7, 1, 3, ELF_UNSUPPORTED_PROFILE},
    {8, 1, 1, ELF_UNSUPPORTED_PROFILE},
    {16, 2, 3, ELF_UNSUPPORTED_PROFILE},
    {18, 2, 183, ELF_UNSUPPORTED_PROFILE},
    {20, 4, 0, ELF_UNSUPPORTED_PROFILE},
    {48, 4, 1, ELF_UNSUPPORTED_PROFILE},
    {52, 2, 63, ELF_BAD_HEADER},
    {54, 2, 55, ELF_BAD_HEADER},
    {56, 2, 0, ELF_LIMIT_EXCEEDED},
    {56, 2, 33, ELF_LIMIT_EXCEEDED},
    {56, 2, 0xffff, ELF_LIMIT_EXCEEDED},
    {32, 8, 63, ELF_BAD_TABLE},
    {32, 8, 480, ELF_BAD_TABLE},
    {32, 8, ~0ULL, ELF_BAD_TABLE},
    {64, 4, 2, ELF_UNSUPPORTED_PROFILE},
    {64, 4, 3, ELF_UNSUPPORTED_PROFILE},
    {64, 4, 6, ELF_UNSUPPORTED_PROFILE},
    {64, 4, 7, ELF_UNSUPPORTED_PROFILE},
    {68, 4, 7, ELF_UNSUPPORTED_PROFILE},
    {68, 4, 1, ELF_UNSUPPORTED_PROFILE},
    {68, 4, 0x105, ELF_UNSUPPORTED_PROFILE},
    {72, 8, 500, ELF_BAD_SEGMENT},
    {72, 8, ~0ULL, ELF_BAD_SEGMENT},
    {96, 8, 65, ELF_BAD_SEGMENT},
    {104, 8, 0, ELF_BAD_SEGMENT},
    {104, 8, ~0ULL, ELF_BAD_SEGMENT},
    {80, 8, 0, ELF_BAD_SEGMENT},
    {80, 8, 1ULL << 47, ELF_BAD_SEGMENT},
    {112, 8, 3, ELF_BAD_SEGMENT},
    {112, 8, 0x200000, ELF_BAD_SEGMENT},
    {136, 8, 0x100100, ELF_BAD_SEGMENT},
    {136, 8, 0x100180, ELF_OVERLAPPING_SEGMENTS},
    {136, 8, 0x80180, ELF_BAD_SEGMENT},
    {104, 8, ELF_MAX_MEMORY_BYTES, ELF_LIMIT_EXCEEDED},
    {24, 8, 0, ELF_BAD_ENTRY},
    {24, 8, 0x100110, ELF_BAD_ENTRY},
    {24, 8, 0x200180, ELF_BAD_ENTRY},
    {24, 8, ~0ULL, ELF_BAD_ENTRY},
    {96, 8, 0, ELF_BAD_ENTRY}
};

// ====== invarianti del piano pubblico ====== //

static int rejected(ELF_VALIDATION_RESULT expected, UINTN size)
{
    plan.Entry = 1;
    plan.TotalMemoryBytes = 1;
    plan.SegmentCount = 1;
    return elf_validate(fixture, size, &plan) == expected &&
        plan.Entry == 0 && plan.TotalMemoryBytes == 0 && plan.SegmentCount == 0;
}

// ====== test limite numero segmenti ====== //

static int segment_limit_test(void)
{
    static UINT8 many[2048];
    UINTN i, j;
    prepare();
    for (i = 0; i < sizeof(many); ++i)
        many[i] = 0;
    for (i = 0; i < 64; ++i)
        many[i] = fixture[i];
    many[56] = 17;
    for (i = 0; i < 17; ++i) {
        UINT64 address = 0x100100 + i * 4096;
        UINT8 *header = many + 64 + i * 56;
        for (j = 0; j < 56; ++j)
            header[j] = fixture[64 + j];
        for (j = 0; j < 8; ++j)
            header[16 + j] = (UINT8)(address >> (j * 8));
    }
    if (elf_validate(many, sizeof(many), &plan) != ELF_LIMIT_EXCEEDED || plan.SegmentCount)
        return 0;
    many[56] = 16;
    return elf_validate(many, sizeof(many), &plan) == ELF_VALID && plan.SegmentCount == 16;
}

// ====== test strutturali e troncamenti ====== //

__declspec(dllexport) int run_elf_tests(void)
{
    UINTN i, j;
    UINT32 state = 0x13579bdf;
    static UINT8 unaligned[513];
    prepare();
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID ||
        plan.Entry != 0x100100 || plan.SegmentCount != 2 || plan.TotalMemoryBytes != 128 ||
        plan.Segments[0].FileOffset != 256 || plan.Segments[0].FileBytes != 16 ||
        plan.Segments[1].Flags != 6 || plan.Segments[1].MemoryBytes != 64)
        return 201;
    for (i = 0; i < sizeof(invalid_cases) / sizeof(invalid_cases[0]); ++i) {
        const INVALID_CASE *test = &invalid_cases[i];
        prepare();
        for (j = 0; j < test->Width; ++j)
            fixture[test->Offset + j] = (UINT8)(test->Value >> (j * 8));
        if (!rejected(test->Expected, sizeof(fixture)))
            return 210 + (int)i;
    }
    prepare();
    for (i = 0; i < 392; ++i) {
        ELF_VALIDATION_RESULT result = elf_validate(fixture, i, &plan);
        if (result == ELF_VALID || plan.SegmentCount != 0 || plan.Entry != 0)
            return 260;
    }
    if (!rejected(ELF_LIMIT_EXCEEDED, ELF_MAX_FILE_BYTES + 1))
        return 261;
    if (elf_validate(0, 0, &plan) != ELF_BAD_ARGUMENT || plan.SegmentCount != 0 ||
        elf_validate(fixture, sizeof(fixture), 0) != ELF_BAD_ARGUMENT)
        return 262;
    prepare();
    put64(112, 0);
    put64(168, 1);
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID)
        return 263;
    prepare();
    put64(152, 0);
    put64(128, 384);
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID)
        return 264;
    prepare();
    put32(120, 0);
    put64(128, ~0ULL);
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID || plan.SegmentCount != 1)
        return 265;
    prepare();
    for (i = 0; i < sizeof(fixture); ++i)
        unaligned[i + 1] = fixture[i];
    if (elf_validate(unaligned + 1, sizeof(fixture), &plan) != ELF_VALID)
        return 266;
    if (!segment_limit_test())
        return 270;
    prepare();
    put32(120, 4);
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID)
        return 271;
    put64(128, ~0ULL);
    if (!rejected(ELF_BAD_SEGMENT, sizeof(fixture)))
        return 272;
    prepare();
    put32(120, 0x6474e551);
    if (elf_validate(fixture, sizeof(fixture), &plan) != ELF_VALID)
        return 273;
    put32(124, 7);
    if (!rejected(ELF_UNSUPPORTED_PROFILE, sizeof(fixture)))
        return 274;

    // ====== mutazioni deterministiche e controllo risultati ====== //

    for (i = 0; i < 5000; ++i) {
        ELF_VALIDATION_RESULT result;
        prepare();
        state = state * 1664525U + 1013904223U;
        fixture[(state >> 8) % sizeof(fixture)] ^= (UINT8)(1U << (state & 7));
        result = elf_validate(fixture, sizeof(fixture), &plan);
        if (result == ELF_VALID) {
            if (!plan.SegmentCount || plan.SegmentCount > ELF_MAX_LOAD_SEGMENTS ||
                plan.TotalMemoryBytes > ELF_MAX_MEMORY_BYTES)
                return 267;
            for (j = 0; j < plan.SegmentCount; ++j) {
                ELF_LOAD_SEGMENT *segment = &plan.Segments[j];
                if (segment->FileOffset > sizeof(fixture) ||
                    segment->FileBytes > sizeof(fixture) - segment->FileOffset ||
                    segment->FileBytes > segment->MemoryBytes || (segment->Flags & 3) == 3)
                    return 268;
            }
        } else if (plan.Entry || plan.TotalMemoryBytes || plan.SegmentCount) {
            return 269;
        }
    }
    return 0;
}
