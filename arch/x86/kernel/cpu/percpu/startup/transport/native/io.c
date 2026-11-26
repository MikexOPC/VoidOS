#include "native.h"
#include "arch/x86/mm/window/access.h"
#include "../../private/frame.h"

// ====== xn_private_parked ======

int xn_private_parked(XN_LAUNCH *m) {
    if (!xn_safe(m) || m->State != 4 || xs_snapshot() != (m->Token | XS_PARKED)) return 0;
    const XS_ENTRY_RECORD *record = &m->Image->Record;
    if (record->Entry != (uint64_t)xs_private_entry ||
        record->StackTop != X86_AP_STACK_TOP(record->Index)) return 0;
    const XS_PRIVATE_FRAME *frame = (const XS_PRIVATE_FRAME *)(record->StackTop - X86_AP_FRAME_BYTES);
    if (frame->Phase != 6 || frame->Error) return 0;
    const unsigned char *a = (const unsigned char *)&frame->Record, *b = (const unsigned char *)record;
    for (size_t i = 0; i < sizeof(*record); ++i) if (a[i] != b[i]) return 0;
    return 1;
}

// ====== xn_read ======

// Intel SDM 253668-078US 11.5.3/11.6.1: ESR +280h and ICR_LOW +300h, 32-bit UC.
int xn_read(void *context, uint32_t offset, uint32_t *value) {
    if (!value || (offset != 0x280 && offset != 0x300) || !xn_safe(context)) return 0;
    return x86_lapic_native_read(offset, value);
}

// ====== xn_write ======

// SDM 078US 11.6.1: high destination[31:24]; low mode[10:8], assert14, edge15=0.
int xn_write(void *context, uint32_t offset, uint32_t value) {
    XN_LAUNCH *m = context;
    if (!xn_safe(m)) return 0;
    uint32_t id = m->Group->Cpus[m->Image->Record.Index].ApicId;
    if (offset == 0x280) { if (value) return 0; }
    else if (offset == 0x310) { if (value != id << 24) return 0; }
    else if (offset == 0x300) {
        if (value == 0x4400) {
            if (m->NmiAttempted || m->Gate != 1 || xs_snapshot() != (m->Token | XS_ARMED)) return 0;
            m->NmiAttempted = 1;
        } else {
            if (m->NmiAttempted || xs_snapshot() != (m->Token | XS_ARMED)) return 0;
            if (value == 0x4500) ++m->InitWrites;
            else if (value == (0x4600U | (uint32_t)(m->Image->Record.Code >> 12))) ++m->SipiWrites;
            else return 0;
        }
    } else return 0;
    return x86_lapic_native_write(offset, value);
}

// ====== gate_private ======

static int gate_private(void *context) {
    uint32_t *value = context;
    const volatile unsigned char *page = x86_window_bytes();
    if (!page) return 0;
    *value = xn_gate_load((const volatile uint32_t *)(page + 104));
    return 1;
}

// ====== xn_ready ======

int xn_ready(void *context, uint64_t token, uint32_t *value) {
    XN_LAUNCH *m = context;
    if (!value || !xn_safe(m) || token != m->Token || m->NmiAttempted ||
        !kernel_physical_lease_state(m->Image->Transition->Pool, &m->Image->Transition->Data, 1)) return 0;
    uint32_t gate;
    if (!x86_apic_borrow(m->Window, m->Image->Record.Data, 0, 0, gate_private, &gate)) return 0;
    if (gate > 1) return 0;
    m->Gate = gate; *value = gate;
    return xn_safe(m);
}

// ====== xn_pause ======

void xn_pause(void *context) { (void)context; ks_pause(); }
