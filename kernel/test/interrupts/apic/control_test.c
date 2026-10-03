#include "arch/x86/kernel/interrupts/apic/control/masked.h"
int test_mask_reset(void);
BOOT_U64 test_mask_physical(void);
int test_mask_quiesce(void);
int test_mask_clean(void);
void test_mask_corrupt(BOOT_U32 kind);
BOOT_U32 test_mask_lapic_version(void);
BOOT_U32 test_mask_io_version(void);
void test_mask_variant(BOOT_U32 maximum, BOOT_U32 inputs, BOOT_U32 pcat);
void test_mask_nmi(BOOT_U32 kind);
static BOOT_U32 local[64], routes[240], selector, calls, fail, stores, bad, port_writes;
static unsigned char master, slave;

// ====== backend controllato del LAPIC senza alcuna istruzione privilegiata ===== //

int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *output)
{
    BOOT_U64 physical = test_mask_physical();
    if (++calls == fail) return 0;
    if (physical == 0xfee00000) {
        if (offset == 0x20) *output = 7U << 24;
        else if (offset == 0x30) *output = test_mask_lapic_version();
        else if (offset >= 0x100 && offset <= 0x270) *output = bad == 2 ? 1 : 0;
        else *output = local[offset / 16];
    } else if (physical == 0xfec00000) {
        if (!offset) *output = selector;
        else if (offset == 0x10) {
            if (!selector) *output = 2U << 24;
            else if (selector == 1) *output = test_mask_io_version();
            else if (selector >= 16 && selector <= 255) *output = routes[selector-16];
            else return 0;
        } else return 0;
    } else return 0;
    return 1;
}
int x86_apic_control_write(BOOT_U32 offset, BOOT_U32 value)
{
    BOOT_U64 physical = test_mask_physical();
    if (++calls == fail) return 0;
    ++stores;
    if (physical == 0xfee00000) local[offset / 16] = bad == 1 ? 0 : value;
    else if (physical == 0xfec00000) {
        if (!offset) selector = value;
        else if (offset == 0x10 && selector >= 16 && selector <= 255) {
            if ((selector & 1) && !(routes[selector-17] & 0x10000)) bad = 3;
            if (bad != 5) routes[selector-16] = value;
        } else return 0;
    } else return 0;
    return 1;
}
unsigned char x86_read_port(unsigned short port) { return port == 0x21 ? master : slave; }
void x86_write_port(unsigned short port, unsigned char value)
{
    if (bad == 6) { ++port_writes; return; }
    if (port == 0x21) master = value;
    else if (port == 0xa1) slave = value;
    else bad = 4;
    ++port_writes;
}

// ====== ogni reset costruisce un profilo verificato e rotte firmware aperte ===== //

static int reset(void)
{
    BOOT_U32 index;
    for (index = 0; index < 64; ++index) local[index] = 0;
    for (index = 0; index < 240; ++index) routes[index] = 0;
    selector = 17; calls = fail = stores = bad = port_writes = 0; master = slave = 0;
    return test_mask_reset();
}

// ====== successo maschere destinazioni selettore e prestito revocato ===== //

__declspec(dllexport) int run_apic_control_tests(void)
{
    BOOT_U32 index, total, failure;
    if (!reset() || !test_mask_quiesce() || !test_mask_clean() || selector != 17 || bad ||
        master != 255 || slave != 255 || port_writes != 2 || local[8] || local[15] != 0x1ff) return 1;
    total = calls;
    for (index = 0; index < 48; ++index)
        if (routes[index] != (index & 1 ? 7U << 24 : 0x10040)) return 2;
    for (index = 0; index <= 5; ++index)
        if (local[x86_irq_lvt(5,index) / 16] != 0x10040) return 3;
    // Un solo errore recuperabile alla volta su ogni operazione della sequenza.
    for (failure = 1; failure <= total; ++failure) {
        if (!reset()) return 4;
        fail = failure;
        if (test_mask_quiesce() || !test_mask_clean()) return 5;
    }
    for (index = 1; index <= 5; ++index) {
        if (!reset()) return 6;
        test_mask_corrupt(index);
        if (test_mask_quiesce() || stores || port_writes || !test_mask_clean()) return 7;
    }
    if (!reset()) return 8;
    bad = 2;
    if (test_mask_quiesce() || stores || port_writes || !test_mask_clean()) return 9;
    if (!reset()) return 10;
    routes[0] = 0x4000;
    if (test_mask_quiesce() || port_writes || !test_mask_clean() || selector != 17) return 11;
    if (!reset()) return 12;
    bad = 1;
    if (test_mask_quiesce() || !test_mask_clean()) return 13;
    if (!reset()) return 14;
    bad = 5;
    if (test_mask_quiesce() || !test_mask_clean()) return 15;
    if (!reset()) return 16;
    bad = 6;
    if (test_mask_quiesce() || !test_mask_clean()) return 17;
    for (index = 3; index <= 6; ++index) {
        if (!reset()) return 18;
        test_mask_variant(index,120,0);
        if (!test_mask_quiesce() || !test_mask_clean() || bad || port_writes || selector != 17) return 19;
        for (failure = 0; failure <= index; ++failure)
            if (local[x86_irq_lvt(index,failure) / 16] != 0x10040) return 20;
        for (failure = 0; failure < 240; ++failure)
            if (routes[failure] != (failure & 1 ? 7U << 24 : 0x10040)) return 21;
    }
    for (index = 1; index <= 5; ++index) {
        int valid = index <= 3;
        if (!reset()) return 22;
        test_mask_nmi(index);
        if (test_mask_quiesce() != valid || !test_mask_clean()) return 23;
        if (!valid && (stores || port_writes)) return 24;
        if (valid && (local[0x350/16] != (index == 2 ? 0x2400U : 0x10040U) ||
            local[0x360/16] != (index == 1 ? 0x400U : 0x10040U))) return 25;
    }
    return 0;
}
