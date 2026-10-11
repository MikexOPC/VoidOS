#include "../../../../../../../arch/x86/kernel/timer/hpet/description/scan.h"

typedef struct {
    uint64_t       Addresses[3];
    unsigned char  Tables[3][X86_HPET_TABLE_BYTES];
    uint32_t       Calls;
    uint32_t       FailCall;
    uint32_t       ChangeCopy;
} SCAN_FIXTURE;

// ====== fix_checksum ======

static void fix_checksum(unsigned char *bytes)
{
    unsigned sum = 0;
    unsigned index;
    bytes[9] = 0;
    for (index = 0; index < 56; ++index) { sum += bytes[index]; }
    bytes[9] = (unsigned char)(0u - sum);
}

// ====== initialize ======

static void initialize(SCAN_FIXTURE *fixture)
{
    unsigned index;
    unsigned offset;
    fixture->Calls = 0;
    fixture->FailCall = 0;
    fixture->ChangeCopy = 0;
    for (index = 0; index < 3; ++index) {
        fixture->Addresses[index] = 0x10000 + index * 0x1000;
        for (offset = 0; offset < 56; ++offset) { fixture->Tables[index][offset] = 0; }
        fixture->Tables[index][0] = 'F';
    }
    fixture->Tables[1][0] = 'H'; fixture->Tables[1][1] = 'P';
    fixture->Tables[1][2] = 'E'; fixture->Tables[1][3] = 'T';
    fixture->Tables[1][4] = 56; fixture->Tables[1][8] = 1;
    fixture->Tables[1][36] = 1;
    fixture->Tables[1][46] = 0xd0; fixture->Tables[1][47] = 0xfe;
    fix_checksum(fixture->Tables[1]);
}

// ====== fixture_read ======

static int fixture_read(void *context, uint64_t physical, uint32_t bytes,
    unsigned char *output)
{
    SCAN_FIXTURE *fixture = context;
    unsigned index;
    unsigned offset;
    ++fixture->Calls;
    if (fixture->Calls == fixture->FailCall || (bytes != 4 && bytes != 56)) {
        return 0;
    }
    for (index = 0; index < 3; ++index) {
        if (physical != fixture->Addresses[index]) { continue; }
        for (offset = 0; offset < bytes; ++offset) {
            output[offset] = fixture->Tables[index][offset];
        }
        if (bytes == 56 && fixture->ChangeCopy) {
            output[0] = 'X'; fix_checksum(output);
        }
        return 1;
    }
    return 0;
}

// ====== expect_result ======

static int expect_result(SCAN_FIXTURE *fixture, const uint64_t *entries,
    uint32_t count, uint32_t bits, int expected, unsigned calls)
{
    X86_HPET_DESCRIPTION output;
    unsigned char *raw = (unsigned char *)&output;
    unsigned index;
    fixture->Calls = 0;
    for (index = 0; index < sizeof(output); ++index) { raw[index] = 0xa5; }
    if (x86_hpet_description_find(entries, count, bits, fixture_read, fixture,
        &output) != expected || fixture->Calls != calls) { return 0; }
    if (expected == X86_HPET_FOUND) {
        return output.Physical == 0xfed00000 && output.BlockId == 1;
    }
    for (index = 0; index < sizeof(output); ++index) {
        if (raw[index] != 0xa5) { return 0; }
    }
    return 1;
}

// ====== run_hpet_scan_tests ======

int run_hpet_scan_tests(void)
{
    SCAN_FIXTURE fixture;
    uint64_t entries[X86_HPET_DIRECTORY_LIMIT];
    X86_HPET_DESCRIPTION output;
    _Alignas(uint64_t) unsigned char aliases[64];
    unsigned index;
    initialize(&fixture);
    if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_FOUND, 4)) { return 1; }
    if (!expect_result(&fixture, NULL, 0, 48, X86_HPET_ABSENT, 0)) { return 2; }
    if (!expect_result(&fixture, fixture.Addresses, 1, 48, X86_HPET_ABSENT, 1)) { return 3; }
    for (index = 0; index < 256; ++index) { entries[index] = fixture.Addresses[0]; }
    if (!expect_result(&fixture, entries, 256, 48, X86_HPET_ABSENT, 256)) { return 4; }
    if (!expect_result(&fixture, entries, 257, 48, X86_HPET_INVALID, 0) ||
        !expect_result(&fixture, NULL, 1, 48, X86_HPET_INVALID, 0) ||
        !expect_result(&fixture, fixture.Addresses, 3, 53, X86_HPET_INVALID, 0)) { return 5; }
    entries[0] = fixture.Addresses[0]; entries[1] = 0;
    if (!expect_result(&fixture, entries, 2, 48, X86_HPET_INVALID, 0)) { return 6; }
    entries[1] = UINT64_MAX;
    if (!expect_result(&fixture, entries, 2, 48, X86_HPET_INVALID, 0)) { return 7; }
    entries[0] = fixture.Addresses[1]; entries[1] = fixture.Addresses[1];
    if (!expect_result(&fixture, entries, 2, 48, X86_HPET_AMBIGUOUS, 3)) { return 8; }
    for (index = 0; index < 56; ++index) { fixture.Tables[2][index] = fixture.Tables[1][index]; }
    if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_AMBIGUOUS, 4)) { return 9; }
    initialize(&fixture);
    for (index = 1; index <= 4; ++index) {
        fixture.FailCall = index;
        if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_READ_FAILED, index)) {
            return (int)(10 + index);
        }
    }
    initialize(&fixture); fixture.Tables[1][10] ^= 1;
    if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_INVALID, 3)) { return 20; }
    initialize(&fixture); fixture.ChangeCopy = 1;
    if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_INVALID, 3)) { return 21; }
    initialize(&fixture); fixture.Addresses[1] = (UINT64_C(1) << 48) - 4;
    if (!expect_result(&fixture, fixture.Addresses, 3, 48, X86_HPET_INVALID, 2)) { return 22; }
    initialize(&fixture); fixture.Calls = 0;
    if (x86_hpet_description_find(fixture.Addresses, 3, 48, NULL, &fixture, &output) !=
        X86_HPET_INVALID || x86_hpet_description_find(fixture.Addresses, 3, 48,
        fixture_read, &fixture, NULL) != X86_HPET_INVALID || fixture.Calls != 0) { return 23; }
    if (!expect_result(&fixture, (const uint64_t *)(const void *)(aliases + 1),
        1, 48, X86_HPET_INVALID, 0)) { return 24; }
    if (x86_hpet_description_find((const uint64_t *)(const void *)aliases, 2, 48,
        fixture_read, &fixture, (X86_HPET_DESCRIPTION *)(void *)(aliases + 8)) !=
        X86_HPET_INVALID || fixture.Calls != 0) { return 25; }
    return 0;
}
