#include "native.h"
#include "../../../internal.h"
#include "arch/x86/mm/window/access.h"
static uint64_t next_code[512], next_data[512];

// ====== equal_private ======

static int equal_private(const void *a, const void *b, size_t bytes) {
    const unsigned char *left = a, *right = b;
    for (size_t i = 0; i < bytes; ++i) if (left[i] != right[i]) return 0;
    return 1;
}

// ====== write_data_private ======

// Intel SDM 253668-078US 9.2.2/12.3: WB stores precede the subsequent UC IPI.
// The previous AP published PARKED after copying its record to private RAM.
static int write_data_private(void *context) {
    const XN_LAUNCH *owner = context;
    if (!owner || owner->State != 3) return 0;
    volatile uint64_t *view = (volatile uint64_t *)x86_window_bytes();
    if (!view) return 0;
    for (uint32_t i = 0; i < 512; ++i) {
        view[i] = next_data[i];
        if (view[i] != next_data[i]) return 0;
    }
    return 1;
}

// ====== xn_next ======

int xn_next(XN_LAUNCH *m) {
    if (!m || m->State != 4 || !xn_safe(m)) return 0;
    XB_INSTALLATION *image = m->Image;
    X86_AP_TRANSITION *t = image->Transition;
    XS_ENTRY_RECORD previous = image->Record, record;
    uint32_t index = previous.Index;
    if (!index || index >= 63 || index + 1 >= m->Group->Header.Count ||
        m->Token != previous.Token || xs_snapshot() != (m->Token | XS_PARKED) ||
        x86_percpu_ap_phase(index) != 2 ||
        xp_panic_value(XP_PANIC_ONLINE) != (UINT64_MAX >> (63 - index)) ||
        !xs_entry_valid(previous, t->Pool->PhysicalBits) ||
        previous.Local != m->Group->Cpus[index].Local ||
        previous.Code != t->Code->Lease.Base || previous.Data != t->Data.Base ||
        previous.TransitionRoot != t->Tables.Base || previous.KernelRoot != t->Root || previous.Pat != t->Pat ||
        !xt_verify_live_tables(t)) return 0;
    if (!xn_private_parked(m)) return 0;
    uint64_t generation = m->Token >> 9;
    if (generation >= UINT32_MAX) return 0;
    uint64_t token = ((generation + 1) << 9) | ((uint64_t)(index + 1) << 3);
    XS_ENTRY_INPUT input = {index + 1, m->Group->Cpus[index + 1].ApicId, t->Pool->PhysicalBits, 0,
        token, previous.Code, previous.Data, previous.TransitionRoot, previous.KernelRoot,
        m->Group->Cpus[index + 1].Local, previous.Entry, previous.Pat};
    if (x86_percpu_ap_phase(index + 1) || !xs_entry_build(&record, input) ||
        !xb_build((unsigned char *)next_code, XB_PAGE, (unsigned char *)next_data, XB_PAGE,
            image->Template, XB_PAGE, record, t->Pool->PhysicalBits) ||
        !equal_private(next_code, image->Code, XB_PAGE) || !xn_safe(m) ||
        !t->Io.Transfer(t->Io.Context, previous.Code, image->Scratch, 0) ||
        !equal_private(image->Scratch, image->Code, XB_PAGE) || !xn_safe(m)) return 0;
    m->State = 3; m->Error = 6;
    if (xs_reserve(index + 1) != token || !xn_safe(m)) return 0;
    m->Error = 7;
    if (!kernel_physical_lease_state(t->Pool, &t->Data, 1)) return 0;
    m->Error = 8;
    if (!x86_apic_borrow(m->Window, t->Data.Base, 0, 1, write_data_private, m)) return 0;
    m->Error = 9;
    if (!xn_safe(m) || !t->Io.Transfer(t->Io.Context, t->Data.Base, image->Scratch, 0)) return 0;
    m->Error = 10;
#ifdef KERNEL_TEST_AP_SEQUENCE_FAULT
    image->Scratch[0] ^= 1;
#endif
    if (!equal_private(image->Scratch, next_data, XB_PAGE)) return 0;
    m->Error = 11;
    if (!xt_verify_live_tables(t) || !xn_safe(m) ||
        xs_snapshot() != (token | XS_PREPARED) ||
        xp_panic_value(XP_PANIC_ONLINE) != (UINT64_MAX >> (63 - index))) return 0;
    for (uint32_t i = 0; i < 512; ++i) image->Data[i] = next_data[i];
    image->Record = record; m->Token = token;
    m->Wake = (XW_RESULT){0};
    m->Last = m->Elapsed = m->Polls = m->Error = m->Gate = 0;
    m->InitWrites = m->SipiWrites = m->NmiAttempted = 0;
    m->State = 1;
    return 1;
}
