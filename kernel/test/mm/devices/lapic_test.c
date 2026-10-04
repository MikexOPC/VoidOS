#include "arch/x86/mm/devices/lapic.h"
int test_mask_reset(void);
int test_mask_clean(void);
int test_lapic_bind(X86_LAPIC_MAPPING *owner);
void test_lapic_fault(BOOT_U32 kind);
BOOT_U64 test_lapic_entry(void);
static X86_LAPIC_MAPPING mapping;

// ====== fixture vergine per ogni ciclo di ownership ===== //

static int reset(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(mapping); ++byte) ((unsigned char *)&mapping)[byte] = 0;
    return test_mask_reset();
}

// ====== lifetime distinto dalla finestra RAM e rilascio senza free fisico ===== //

__declspec(dllexport) int run_lapic_mapping_tests(void)
{
    BOOT_U32 index;
    if (!reset() || !test_lapic_bind(&mapping) || !x86_lapic_validate(&mapping) ||
        !test_lapic_entry() || !test_mask_clean()) return 1;
    if (test_lapic_bind(&mapping)) return 2;
    if (!x86_lapic_unbind(&mapping) || mapping.Ready || mapping.Self ||
        test_lapic_entry() || !test_mask_clean() || x86_lapic_unbind(&mapping)) return 3;
    for (index = 1; index <= 3; ++index) {
        if (!reset()) return 4;
        test_lapic_fault(index);
        if (test_lapic_bind(&mapping) || mapping.Ready || !test_mask_clean()) return 5;
    }
    if (!reset() || !test_lapic_bind(&mapping)) return 6;
    test_lapic_fault(4);
    if (x86_lapic_validate(&mapping) || x86_lapic_unbind(&mapping) || !test_lapic_entry()) return 7;
    if (!reset() || !test_lapic_bind(&mapping)) return 8;
    mapping.Self = 0;
    if (x86_lapic_validate(&mapping) || x86_lapic_unbind(&mapping)) return 9;
    if (!reset() || !test_lapic_bind(&mapping)) return 10;
    if (x86_lapic_read(&mapping,0x120,(BOOT_U32 *)&mapping) ||
        x86_lapic_read(&mapping,0x121,&index) || x86_lapic_read(&mapping,0xb0,&index) ||
        x86_lapic_read(&mapping,0x180,&index) || x86_lapic_read(&mapping,0x280,&index)) return 11;
    mapping.PhysicalBits = 31;
    if (x86_lapic_validate(&mapping) || x86_lapic_unbind(&mapping)) return 12;
    return 0;
}
