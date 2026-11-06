#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
int test_uart_plan(X86_UART_ROUTE *route);
int test_uart_apply(const X86_UART_ROUTE *route, BOOT_U32 masked);
void test_uart_owner(X86_UART_IRQ_OWNER *state);
void test_uart_variant(BOOT_U32 kind);
int test_mask_reset(void);
BOOT_U64 test_mask_physical(void);
int test_mask_quiesce(void);
int test_mask_clean(void);
void test_mask_corrupt(BOOT_U32 kind);
BOOT_U32 test_mask_lapic_version(void);
BOOT_U32 test_mask_io_version(void);
void test_mask_variant(BOOT_U32 maximum, BOOT_U32 inputs, BOOT_U32 pcat);
void test_mask_nmi(BOOT_U32 kind);
BOOT_U64 test_lapic_entry(void);
void test_lapic_ram_busy(BOOT_U32 busy);
static BOOT_U32 local[64], routes[240], selector, calls, fail, stores, bad, port_writes;
static unsigned char master, slave;
static unsigned char ier, iir, lcr;
static BOOT_U32 in_service, eois, uart_bad, irr_pending;
static BOOT_U32 tx_writes;
static unsigned char tx_last;

// ====== backend controllato del LAPIC senza alcuna istruzione privilegiata ===== //

int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *output)
{
    BOOT_U64 physical = test_mask_physical();
    if (++calls == fail) return 0;
    if (physical == 0xfee00000) {
        if (offset == 0x20) *output = 7U << 24;
        else if (offset == 0x30) *output = test_mask_lapic_version();
        else if (offset >= 0x100 && offset <= 0x270)
            *output = bad == 2 ? 1 : (offset == 0x120 ? in_service : 0);
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
unsigned char x86_read_port(unsigned short port)
{
    if (port == 0x3f9) return ier;
    if (port == 0x3fa) { unsigned char value = iir; iir = 0xc1; return value; }
    if (port == 0x3fb) return lcr;
    if (port == 0x3fd) return 0x60;
    return port == 0x21 ? master : slave;
}
void x86_write_port(unsigned short port, unsigned char value)
{
    if (bad == 6) { ++port_writes; return; }
    if (port == 0x3f9) { if (uart_bad != 1) ier = value; return; }
    if (port == 0x3fc) return;
    if (port == 0x3f8) { ++tx_writes; tx_last = value; iir = 0xc2; return; }
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
    ier = 2; iir = 0xc2; lcr = 3; in_service = eois = uart_bad = irr_pending = 0;
    tx_writes = 0; tx_last = 0;
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

// ====== EOI modello exige ACK precedente e non modifica altri ISR ===== //

int x86_lapic_native_eoi(void)
{
    if ((test_lapic_entry() & 0x000ffffffffff000ULL) != 0xfee00000 ||
        (in_service != 1 && in_service != 2 && in_service != 4 && in_service != 8) || (in_service == 1 && ier && !tx_writes)) return 0;
    ++eois;
    if (uart_bad != 2) in_service = 0;
    return 1;
}

// ====== lettura LAPIC persistente senza usare la finestra RAM ===== //

int x86_lapic_native_read(BOOT_U32 offset, BOOT_U32 *value)
{
    if (++calls == fail) return 0;
    *value = offset == 0x120 ? in_service : offset == 0x220 ? irr_pending : 0;
    return 1;
}
// ====== attesa privilegiata esclusa dal processo host ===== //

int x86_uart_irq_wait(volatile BOOT_U32 *counts, BOOT_U32 goal, BOOT_U32 budget)
{
    (void)counts; (void)goal; (void)budget;
    return 0;
}

// ====== unico ISR timer e rifiuto di EOI per vettore errato o due ISR ===== //

__declspec(dllexport) int run_lapic_timer_eoi_tests(void)
{
    X86_UART_IRQ_OWNER state;
    if (!reset()) return 1;
    test_uart_owner(&state);
    in_service = 2;
    if (x86_lapic_eoi(state.Mapping) || eois ||
        !x86_lapic_eoi_vector(state.Mapping,0x41) || eois != 1 || in_service) return 2;
    in_service = 3;
    if (x86_lapic_eoi_vector(state.Mapping,0x41) || eois != 1 || in_service != 3) return 3;
    in_service = 2; uart_bad = 2;
    if (x86_lapic_eoi_vector(state.Mapping,0x41) || eois != 2 || in_service != 2) return 4;
    uart_bad = 0;
    if (!x86_lapic_eoi_vector(state.Mapping,0x41) || !reset()) return 5;
    test_uart_owner(&state); in_service = 4;
    if (x86_lapic_eoi_vector(state.Mapping,0x41) || x86_lapic_eoi_vector(state.Mapping,0x43) || eois ||
        !x86_lapic_eoi_vector(state.Mapping,0x42) || eois != 1 || in_service) return 6;
    for (BOOT_U32 extra = 1; extra <= 3; ++extra) {
        in_service = 4 | extra;
        if (x86_lapic_eoi_vector(state.Mapping,0x42) || eois != 1 || in_service != (4 | extra)) return 7;
    }
    in_service = 4; uart_bad = 2;
    if (x86_lapic_eoi_vector(state.Mapping,0x42) || eois != 2 || in_service != 4) return 8;
    uart_bad = 0;
    if (!x86_lapic_eoi_vector(state.Mapping,0x42) || in_service) return 9;
    if (!x86_lapic_idle(state.Mapping) || !x86_lapic_idle_except(state.Mapping,0) ||
        !x86_lapic_idle_except(state.Mapping,6) || x86_lapic_idle_except(state.Mapping,16)) return 10;
    irr_pending = 2;
    if (x86_lapic_idle(state.Mapping) || x86_lapic_idle_except(state.Mapping,4) ||
        !x86_lapic_idle_except(state.Mapping,2) || !x86_lapic_idle_except(state.Mapping,6)) return 11;
    irr_pending = 6;
    if (x86_lapic_idle_except(state.Mapping,2) || !x86_lapic_idle_except(state.Mapping,6)) return 12;
    irr_pending = 8;
    if (x86_lapic_idle_except(state.Mapping,7) || !x86_lapic_idle_except(state.Mapping,8)) return 13;
    irr_pending = 0; in_service = 2;
    if (x86_lapic_idle_except(state.Mapping,2) || eois != 3) return 14;
    in_service = 8;
    if (x86_lapic_eoi_vector(state.Mapping,0x44) || x86_lapic_eoi_vector(state.Mapping,0x42) ||
        !x86_lapic_eoi_vector(state.Mapping,0x43) || in_service || eois != 4) return 15;
    in_service = 9;
    if (x86_lapic_eoi_vector(state.Mapping,0x43) || eois != 4 || in_service != 9) return 16;
    in_service = 8; uart_bad = 2;
    if (x86_lapic_eoi_vector(state.Mapping,0x43) || eois != 5 || in_service != 8) return 17;
    uart_bad = 0;
    if (!x86_lapic_eoi_vector(state.Mapping,0x43) || in_service || eois != 6) return 18;
    return 0;
}

// ====== TX integra scrittura ACK EOI e contatori senza confermare guasti ===== //

__declspec(dllexport) int run_uart_tx_irq_tests(void)
{
    X86_UART_IRQ_OWNER state;
    X86_UART_TX buffer = {0};
    unsigned char source[2] = {0x31,0x72};
    BOOT_U32 step;
    if (!reset()) return 1;
    test_uart_owner(&state); state.Count = 3; state.TxBase = 3; state.Tx = &buffer;
    if (!x86_uart_tx_prepare(&buffer,source,2)) return 2;
    for (step = 0; step < 3; ++step) {
        in_service = 1;
        if (!x86_uart_irq_service(&state) || state.Count != step+4 || state.Error ||
            eois != step+1 || in_service || tx_writes != (step < 2 ? step+1 : 2) ||
            (step < 2 && tx_last != source[step])) return 3;
    }
    if (!buffer.Done || ier || !x86_uart_tx_drain(&buffer)) return 4;
    state.Active = 0; state.Tx = 0;
    if (!x86_uart_tx_release(&buffer,state.Count-state.TxBase) ||
        !x86_uart_tx_prepare(&buffer,source,1)) return 9;
    state.TxBase = state.Count; state.Tx = &buffer; state.Active = 1;
    ier = 2; iir = 0xc2; in_service = 1;
    if (!x86_uart_irq_service(&state) || state.Count != 7 || tx_writes != 3) return 10;
    in_service = 1;
    if (!x86_uart_irq_service(&state) || state.Count != 8 || eois != 5 ||
        !buffer.Done || !x86_uart_tx_release(&buffer,state.Count-state.TxBase)) return 11;
    for (step = 0; step < sizeof(buffer); ++step) ((unsigned char *)&buffer)[step] = 0;
    if (!reset()) return 5;
    test_uart_owner(&state); state.Count = 3; state.TxBase = 3; state.Tx = &buffer; in_service = 1;
    if (!x86_uart_tx_prepare(&buffer,source,2)) return 6;
    uart_bad = 2;
    if (x86_uart_irq_service(&state) || state.Count != 3 || state.Error != 2 ||
        tx_writes != 1 || !in_service || eois != 1 || ier) return 7;
    for (step = 0; step < sizeof(buffer); ++step) ((unsigned char *)&buffer)[step] = 0;
    if (!reset()) return 12;
    test_uart_owner(&state); state.Count = 3; state.TxBase = 3; state.Tx = &buffer;
    if (!x86_uart_tx_prepare(&buffer,source,1)) return 13;
    in_service = 1;
    if (!x86_uart_irq_service(&state)) return 14;
    in_service = 1; uart_bad = 2;
    if (x86_uart_irq_service(&state) || state.Count != 4 || state.Error != 2 || !buffer.Done ||
        x86_uart_tx_release(&buffer,state.Count-state.TxBase) || !buffer.Self) return 15;
    if (!reset()) return 8;
    return 0;
}

// ====== rotta ACK EOI revoca e guasti su ogni accesso controller ===== //

__declspec(dllexport) int run_uart_irq_tests(void)
{
    X86_UART_ROUTE route;
    X86_UART_IRQ_OWNER owner;
    BOOT_U32 index, total;
    if (!reset() || !test_uart_plan(&route) || route.Gsi != 4 || route.Pin != 4 ||
        route.Bits != 0x40 || route.Destination != 7) return 1;
    if (!test_uart_apply(&route,0) || routes[8] != 0x40 || routes[9] != 7U << 24 ||
        selector != 17 || !test_mask_clean()) return 2;
    total = calls;
    for (index = 1; index <= total; ++index) {
        if (!reset() || !test_uart_plan(&route)) return 3;
        fail = index;
        if (test_uart_apply(&route,0) || !test_mask_clean()) return 4;
    }
    for (index = 1; index <= 6; ++index) {
        if (!reset()) return 5;
        test_uart_variant(index);
        if (test_uart_plan(&route) != (index == 1)) return 6;
        if (index == 1 && (route.Pin != 20 || route.Bits != 0xa040)) return 7;
    }
    if (!reset() || !test_uart_plan(&route)) return 8;
    route.Pin = 120;
    if (test_uart_apply(&route,0) || stores || !test_mask_clean()) return 9;
    for (index = 0; index < 3; ++index) {
        if (!reset()) return 10;
        test_uart_owner(&owner); in_service = 1;
        owner.Count = index;
        if (!x86_uart_irq_service(&owner) || owner.Count != index+1 || owner.Error ||
            ier || in_service || eois != 1 || !test_mask_clean()) return 11;
    }
    // Causa inattesa: IER chiuso ed EOI, senza attestare un evento riuscito.
    if (!reset()) return 12;
    test_uart_owner(&owner); in_service = 1; iir = 0xc4;
    if (x86_uart_irq_service(&owner) || owner.Count || owner.Error != 1 ||
        ier || in_service || eois != 1 || !test_mask_clean()) return 13;
    for (index = 1; index <= 5; ++index) {
        if (!reset()) return 14;
        test_uart_owner(&owner); in_service = 1; uart_bad = index;
        if (index == 3) in_service = 0;
        if (index == 4) iir = 0x12;
        if (index == 5) lcr = 0x83;
        if (x86_uart_irq_service(&owner) || !owner.Error || owner.Count || !test_mask_clean()) return 15;
        if (index == 1 && eois) return 16;
    }
    if (!reset()) return 17;
    test_uart_owner(&owner); owner.Count = 3;
    if (x86_uart_irq_service(&owner) || calls || eois) return 18;
    if (!reset()) return 19;
    test_uart_owner(&owner); in_service = 1;
    if (!x86_uart_irq_service(&owner)) return 20;
    total = calls;
    for (index = 1; index <= total; ++index) {
        if (!reset()) return 21;
        test_uart_owner(&owner); in_service = 1; fail = index;
        if (x86_uart_irq_service(&owner) || owner.Count || owner.Error != 2 ||
            ier || !test_mask_clean()) return 22;
    }
    if (!reset()) return 23;
    test_uart_owner(&owner); owner.Active = 0;
    if (x86_uart_irq_service(&owner) || calls || eois) return 24;
    if (!reset()) return 25;
    test_uart_owner(&owner); in_service = 1;
    test_lapic_ram_busy(1);
    if (!x86_uart_irq_service(&owner) || owner.Count != 1 || eois != 1) return 26;
    test_lapic_ram_busy(0);
    if (!test_mask_clean()) return 27;
    return 0;
}
