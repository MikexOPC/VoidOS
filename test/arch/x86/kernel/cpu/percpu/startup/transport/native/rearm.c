#include "arch/x86/kernel/cpu/percpu/startup/transport/native/native.h"
#include "arch/x86/mm/window/access.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static XN_LAUNCH owner;
static XAG_GROUP group;
static XB_INSTALLATION image;
static X86_AP_TRANSITION transition;
static X86_AP_LOW_MEMORY low;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static unsigned char template_bytes[4096];
static uint64_t physical_code[512], physical_data[512];
static const char *mode;
static unsigned int previous, count, borrowed, reads, live_calls;
static int safe = 1;

// ====== modeled_native_boundaries ======

int xn_safe(void *context) { return context == &owner && safe && strcmp(mode, "owner"); }
int xn_private_parked(XN_LAUNCH *m) { assert(m == &owner); return strcmp(mode, "private") != 0; }
uint64_t x86_percpu_ap_phase(uint32_t index) {
    if (index == previous) return !strcmp(mode, "phase") ? 0 : 2;
    return !strcmp(mode, "future-phase") ? 2 : 0;
}
uint64_t xp_panic_value(uint32_t field) {
    assert(field == XP_PANIC_ONLINE);
    return !strcmp(mode, "online") ? 1 : UINT64_MAX >> (63 - previous);
}
int xt_verify_live_tables(X86_AP_TRANSITION *t) {
    assert(t == &transition); ++live_calls;
    return strcmp(mode, "tables") && (strcmp(mode, "post-tables") || live_calls != 2);
}
int kernel_physical_lease_state(const KERNEL_PHYSICAL_ALLOCATOR *p, const KERNEL_PHYSICAL_LEASE *lease, uint32_t pin) {
    assert(p == &pool && lease == &transition.Data && pin == 1);
    return strcmp(mode, "pin") != 0;
}
volatile unsigned char *x86_window_bytes(void) {
    return !strcmp(mode, "no-view") ? NULL : (volatile unsigned char *)physical_data;
}
int x86_apic_borrow(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache, int writable,
    int (*operation)(void *), void *context) {
    assert(window == owner.Window && physical == transition.Data.Base && cache == 0 && writable == 1 && context == &owner);
    ++borrowed;
    if (!strcmp(mode, "borrow")) return 0;
    if (!strcmp(mode, "partial")) { physical_data[0] = 0; return 0; }
    int result = operation(context);
    if (!strcmp(mode, "safe-after-write")) safe = 0;
    return result;
}

// ====== transfer_private ======

static int transfer_private(void *context, BOOT_U64 physical, BOOT_U64 *page, int write) {
    assert(context == &transition && write == 0); ++reads;
    if (physical == low.Lease.Base) {
        if (!strcmp(mode, "code-read")) return 0;
        memcpy(page, physical_code, 4096);
    } else {
        assert(physical == transition.Data.Base);
        if (!strcmp(mode, "readback")) return 0;
        memcpy(page, physical_data, 4096);
        if (!strcmp(mode, "data-corrupt")) page[500] ^= 1;
    }
    return 1;
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 3); mode = argv[2];
    FILE *file = NULL;
    int opened = fopen_s(&file, argv[1], "rb");
    assert(opened == 0 && file);
    size_t bytes = fread(template_bytes, 1, sizeof(template_bytes), file);
    assert(bytes == sizeof(template_bytes));
    int closed = fclose(file);
    assert(closed == 0);
    previous = !strcmp(mode, "last") ? 3 : !strcmp(mode, "high-index") ? 62 : 1;
    count = previous > 3 ? 64 : 4;
    assert(xs_configure(count));
    for (unsigned int i = 1; i <= previous; ++i) {
        owner.Token = xs_reserve(i); assert(owner.Token);
        for (uint64_t phase = XS_PREPARED; phase <= XS_PRIVATE; ++phase) assert(xs_advance(owner.Token, phase));
    }
    group.Header.Count = count; group.Header.PhysicalBits = 40;
    for (unsigned int i = 0; i < count; ++i) {
        group.Cpus[i].Local = 0xffffffff80010000ULL + i * 448;
        group.Cpus[i].ApicId = i;
    }
    pool.PhysicalBits = 40; low.Lease.Base = 0x8000;
    transition.Pool = &pool; transition.Code = &low;
    transition.Data.Base = 0x9000; transition.Data.Pages = 1;
    transition.Tables.Base = 0x10000; transition.Root = 0x20000; transition.Pat = 6;
    transition.Io = (XT_IO){transfer_private, NULL, &transition};
    XS_ENTRY_INPUT input = {previous, previous, 40, 0, owner.Token, 0x8000, 0x9000, 0x10000,
        0x20000, group.Cpus[previous].Local, 0xffffffff80001000ULL, 6};
    assert(xs_entry_build(&image.Record, input));
    image.Transition = &transition; image.Template = template_bytes;
    assert(xb_build((unsigned char *)image.Code, 4096, (unsigned char *)image.Data, 4096,
        template_bytes, 4096, image.Record, 40));
    memcpy(physical_code, image.Code, 4096); memcpy(physical_data, image.Data, 4096);
    physical_data[13] = 3;
    if (!strcmp(mode, "code-corrupt")) physical_code[500] ^= 1;
    owner.Image = &image; owner.Group = &group; owner.State = 4;
    owner.InitWrites = owner.SipiWrites = owner.NmiAttempted = owner.Polls = 42;
    owner.Window = (X86_PAGE_WINDOW *)&pool;
    uint64_t old_token = owner.Token;
    if (!strcmp(mode, "token")) owner.Token ^= 512;
    if (!strcmp(mode, "record")) image.Record.KernelRoot ^= 4096;
    if (!strcmp(mode, "index")) image.Record.Index = 0;
    XS_ENTRY_RECORD old_record = image.Record;
    int result = xn_next(&owner);
    if (!strcmp(mode, "positive") || !strcmp(mode, "high-index")) {
        assert(result && owner.State == 1 && !owner.Error && owner.Image->Record.Index == previous + 1);
        assert(xs_snapshot() == (owner.Token | XS_PREPARED) && owner.Token != old_token);
        assert(borrowed == 1 && reads == 2 && live_calls == 2);
        assert(!owner.InitWrites && !owner.SipiWrites && !owner.NmiAttempted && !owner.Polls);
        assert(memcmp(physical_code, image.Code, 4096) == 0 && memcmp(physical_data, image.Data, 4096) == 0);
        assert(physical_data[13] == 0);
        unsigned int writes = borrowed;
        assert(!xn_next(&owner) && borrowed == writes);
    } else {
        assert(!result && memcmp(&image.Record, &old_record, sizeof(old_record)) == 0);
        int terminal = !strcmp(mode, "pin") || !strcmp(mode, "borrow") || !strcmp(mode, "partial") ||
            !strcmp(mode, "no-view") || !strcmp(mode, "readback") || !strcmp(mode, "data-corrupt") ||
            !strcmp(mode, "post-tables") || !strcmp(mode, "safe-after-write");
        if (terminal) {
            assert(owner.State == 3 && owner.Error && xs_snapshot() != (old_token | XS_PARKED));
            unsigned int writes = borrowed;
            assert(!xn_next(&owner) && borrowed == writes);
        } else assert(owner.State == 4 && !borrowed && xs_snapshot() == (old_token | XS_PARKED));
    }
    printf("PASS: rearm %s; actual Rust coordinator/record/blob policy, modeled memory boundaries\n", mode);
    return 0;
}
