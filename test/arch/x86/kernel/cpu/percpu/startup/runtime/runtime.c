#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "arch/x86/kernel/cpu/percpu/startup/runtime/runtime.c"

static X86_CPU_IDENTITIES identities;
static X86_CPU_LOCAL local;
static KERNEL_LOCK_CONTEXT locks;
static X86_AP_TRANSITION transition;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_AP_LOW_MEMORY low;
static X86_LAPIC_MAPPING lapic;
static uint32_t step, fail_at, committed, erases, cancels, runs, nexts, parked;
static uint64_t snapshot, online;
static uint64_t irq_state;
static uint32_t boot_cpu;
static int stop_expected, rollback_broken;
static jmp_buf stopped;
XP_PANIC_NATIVE xp_panic_native;
XIS_NATIVE xis_native;
static uint32_t pings, ping_failure;
const unsigned char xs_blob_template[4096] = {0};
const unsigned char xs_private_entry[1] = {0};

// ====== pass_private ======

static int pass_private(void) { return ++step != fail_at; }
const X86_CPU_IDENTITIES *x86_cpu_identities(void) { return &identities; }
KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) { return &locks; }
uint64_t ks_irq_state(void) { return irq_state; }
uint32_t ks_boot_cpu(void) { return boot_cpu; }
int kernel_text(const char *s) { return s != NULL; }
int kernel_number(BOOT_U64 n) { (void)n; return 1; }
int kernel_log(unsigned int level, const char *s) { (void)level; return s != NULL; }
const X86_CPU_LOCAL *x86_percpu_ap(uint32_t i) { assert(i == 1); return &local; }
int xj_retained(const void *p) { assert(p == &transition); return committed != 0; }
int xb_erase(XB_INSTALLATION *p) { ++erases; assert(!committed && p == &image); memset(p, 0, sizeof(*p)); return !rollback_broken; }
int xt_cancel(X86_AP_TRANSITION *p) { ++cancels; assert(!committed && p == &transition); return !rollback_broken; }
__declspec(noreturn) void ks_fail_stop(const char *s) { assert(stop_expected && !strcmp(s, "AP_RUNTIME_ROLLBACK_FAILED")); longjmp(stopped, 1); }
uint32_t xs_configure(uint32_t n) { assert(n == identities.Count); return pass_private(); }
uint64_t xs_reserve(uint32_t i) { assert(i == 1); return pass_private() ? 520 : 0; }

// ====== installation_model ======

int xs_entry_build(XS_ENTRY_RECORD *r, XS_ENTRY_INPUT in) {
    assert(in.Index == 1 && in.Entry == (uint64_t)xs_private_entry && in.Local == (uint64_t)&local);
    memset(r, 0, sizeof(*r)); r->Token = in.Token; r->Index = 1;
    return pass_private();
}
int xb_install(XB_INSTALLATION *p, X86_AP_TRANSITION *t, const unsigned char *b, XS_ENTRY_RECORD r) {
    assert(p == &image && t == &transition && b == xs_blob_template);
    p->Self = (uint64_t)p; p->Transition = t; p->Record = r;
    return pass_private();
}
int xj_native_operations(XJ_OPERATIONS *o) { memset(o, 0, sizeof(*o)); o->Cpus = identities.Count; return pass_private(); }
int xj_commit(XJ_GROUP *g, XB_INSTALLATION *p, const XJ_OPERATIONS *o) {
    assert(g == &group && p == &image && o->Cpus == identities.Count);
    if (!pass_private()) return 0;
    committed = 1; return 1;
}
int xp_panic_native_prepare(const X86_LAPIC_MAPPING *p) { assert(p == &lapic); return pass_private(); }
int xag_native_publish(const XJ_GROUP *g) { assert(committed && g == &group); return pass_private(); }
int xag_native_close(void) { assert(committed); return pass_private(); }

// ====== transport_model ======

int xn_prepare(XN_LAUNCH *m, const XJ_GROUP *g) {
    assert(committed && g == &group); m->Image = &image; m->Token = 520;
    return pass_private();
}
int xn_next(XN_LAUNCH *m) {
    ++nexts; assert(runs == nexts && m == &launch);
    ++image.Record.Index; m->Token = ((uint64_t)image.Record.Index << 9) | ((uint64_t)image.Record.Index << 3);
    return pass_private();
}
int xn_run(XN_LAUNCH *m, int nmi) {
    assert(committed && nmi == 1 && m == &launch && image.Record.Index == runs + 1);
    if (!pass_private()) return 0;
    ++runs; m->State = 4; snapshot = m->Token | XS_PARKED; online |= 1ULL << runs; return 1;
}
int xn_private_parked(XN_LAUNCH *m) { assert(m == &launch); ++parked; return pass_private(); }
int xt_verify_live_tables(X86_AP_TRANSITION *t) { assert(t == &transition); return pass_private(); }
uint64_t xs_snapshot(void) { return snapshot; }
uint64_t xp_panic_value(uint32_t field) { assert(field == XP_PANIC_ONLINE || field == XP_PANIC_OWNER); return field == XP_PANIC_ONLINE ? online : 0; }

// ====== service_model ======

int xis_native_prepare(void) { assert(committed && !runs); return pass_private(); }
int xis_native_wait(uint32_t index, uint32_t field, uint64_t value) {
    assert(runs == identities.Count - 1 && index == pings + 1 && !field && value == XIS_READY);
    return ping_failure != 1;
}
uint64_t xis_publish(uint32_t index, uint32_t caller) {
    assert(index == pings + 1 && !caller); return ping_failure == 2 ? 0 : 1;
}
int xis_native_send(uint32_t index) { assert(index == pings + 1); return ping_failure != 3; }
int xis_native_now(void *context, uint32_t *now) { (void)context; (void)now; return 0; }
uint32_t xis_wait(uint32_t index, uint64_t generation, const XIS_WAIT *wait) {
    assert(index == pings + 1 && generation == 1 && wait->Read == xis_native_now && wait->MaxPolls);
    if (ping_failure == 4) return XIS_WAIT_FAILED;
    ++pings;
    return XIS_WAIT_ACK;
}
__declspec(noreturn) void xis_native_abort(uint64_t reason) {
    assert(ping_failure && reason == 0x4b3449504950494eULL); longjmp(stopped, 1);
}

// ====== reset_private ======

static void reset_private(uint32_t count) {
    memset(&image, 0, sizeof(image)); memset(&group, 0, sizeof(group)); memset(&launch, 0, sizeof(launch));
    memset(&transition, 0, sizeof(transition)); memset(&identities, 0, sizeof(identities));
    memset(&locks, 0, sizeof(locks)); attempted = 0;
    identities.Count = count; transition.Self = (uint64_t)&transition; transition.State = 3;
    transition.Pool = &pool; transition.Code = &low; pool.PhysicalBits = 48;
    step = fail_at = committed = erases = cancels = runs = nexts = parked = 0;
    snapshot = 0; online = 1; stop_expected = rollback_broken = 0;
    pings = ping_failure = 0;
    irq_state = 0; boot_cpu = 1;
}

// ====== expect_private ======

static void expect_private(X86_AP_TRANSITION *t, const X86_LAPIC_MAPPING *l, int expected) {
    int result = x86_ap_runtime_start(t, l);
    assert(result == expected);
}

// ====== main ======

int main(void) {
    reset_private(4);
    expect_private(&transition, &lapic, 1);
    assert(pings == 3);
    uint32_t stages = step;
    assert(committed && runs == 3 && nexts == 2 && parked == 3 && !erases && !cancels && online == 15);
    expect_private(&transition, &lapic, 0); assert(step == stages);
    for (uint32_t failure = 1; failure <= stages; ++failure) {
        reset_private(4); fail_at = failure;
        expect_private(&transition, &lapic, 0);
        assert((committed && !erases && !cancels) || (!committed && cancels == 1));
        assert(runs <= 3 && nexts <= 2);
    }
    reset_private(64); expect_private(&transition, &lapic, 1); assert(runs == 63 && nexts == 62 && online == UINT64_MAX);
    reset_private(1); expect_private(&transition, &lapic, 1); assert(!committed && !runs && step == 1);
    reset_private(4); locks.Depth = 1; expect_private(&transition, &lapic, 0); assert(!step && !attempted);
    reset_private(4); irq_state = 0x200; expect_private(&transition, &lapic, 0); assert(!step && !attempted);
    reset_private(4); boot_cpu = 0; expect_private(&transition, &lapic, 0); assert(!step && !attempted);
    reset_private(0); expect_private(&transition, &lapic, 0); assert(!step);
    reset_private(65); expect_private(&transition, &lapic, 0); assert(!step);
    reset_private(4); expect_private(NULL, &lapic, 0); assert(!step);
    reset_private(4); expect_private(&transition, NULL, 0); assert(!step);
    reset_private(4); transition.Pool = NULL; expect_private(&transition, &lapic, 0); assert(!step);
    reset_private(4); transition.Code = NULL; expect_private(&transition, &lapic, 0); assert(!step);
    reset_private(4); transition.Self = 0; expect_private(&transition, &lapic, 0); assert(!step);
    reset_private(4); rollback_broken = stop_expected = 1; fail_at = 4;
    if (!setjmp(stopped)) { (void)x86_ap_runtime_start(&transition, &lapic); assert(0); }
    for (uint32_t failure = 1; failure <= 4; ++failure) {
        reset_private(4); ping_failure = failure;
        if (!setjmp(stopped)) { (void)x86_ap_runtime_start(&transition, &lapic); assert(0); }
        assert(committed && runs == 3 && !pings && !erases && !cancels);
    }
    printf("AP_RUNTIME_HOST_PASS: %u failure stages, order, duplicate, BSP-only, bounds and terminal rollback\n", stages);
    return 0;
}
