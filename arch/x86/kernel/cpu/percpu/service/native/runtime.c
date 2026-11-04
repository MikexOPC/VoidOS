#include <intrin.h>
#include "arch/x86/configs/boot_profile.h"
#include "native.h"
#include "../../startup/private/frame.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "arch/x86/kernel/stack/main.h"

XIS_NATIVE xis_native;
#ifdef KERNEL_TEST_AP_SERVICE
uint64_t xis_probe_base, xis_probe_armed;
#endif
#if KERNEL_TEST_AP_SERVICE == 6
uint64_t xis_release_armed;
#endif

// ====== xis_native_abort ======

__declspec(noreturn) void xis_native_abort(uint64_t reason) { xp_panic_voluntary(reason); }

// ====== xis_native_index ======

uint32_t xis_native_index(void) {
    if (!ks_atomic_load_acquire(&xis_native.Published) || !ks_atomic_load_acquire(&xp_panic_armed) ||
        xis_native.Count < 2 || xis_native.Count > 64 || xis_native.Count != xp_panic_native.Count ||
        x86_kernel_root() != xp_panic_native.Root) xis_native_abort(0x4b34495049494400ULL);
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner || owner->Index >= xis_native.Count || owner != xp_panic_native.Contexts[owner->Index] ||
        owner->State != X86_PERCPU_ACTIVE || owner->Self != (uint64_t)owner || owner->Locks.Depth ||
        xp_panic_apic_id() != xp_panic_native.Ids[owner->Index]) xis_native_abort(0x4b34495049494401ULL);
    return owner->Index;
}

// ====== xis_native_prepare ======

int xis_native_prepare(void) {
    if (ks_irq_state() || !ks_boot_cpu() || ks_atomic_load_acquire(&xis_native.Published) ||
        xp_panic_native.Count < 2 || xp_panic_native.Count > 64 ||
        !ks_atomic_load_acquire(&xp_panic_armed) || !x86_lapic_authority(xp_panic_native.Lapic) ||
        !x86_irq_prepare(&xis_native.Idt, &xis_native.Idtr, &xp_panic_native.Idt,
            xis_irq_targets, (uint64_t)xis_irq_begin, (uint64_t)xis_irq_end) || !xis_configure(xp_panic_native.Count)) return 0;
    xis_native.Count = xp_panic_native.Count;
    ks_atomic_store_release(&xis_native.Published, 1);
    return 1;
}

// ====== empty_private ======

static int empty_private(void) {
    for (uint32_t word = 0; word < 8; ++word)
        if (xis_mmio_read(0x100 + word * 16) || xis_mmio_read(0x200 + word * 16)) return 0;
    return 1;
}

// ====== xis_native_enter ======

uint32_t xis_native_enter(void *private_frame) {
    if (ks_irq_state()) xis_native_abort(0x4b34495049494600ULL);
    uint32_t index = xis_native_index();
    const XS_PRIVATE_FRAME *frame = private_frame;
    uint64_t rsp = x86_kernel_stack_pointer();
    if (!index || (uint64_t)frame != X86_AP_FRAME(index) || frame->Phase != 6 || frame->Error ||
        rsp < X86_AP_STACK_FLOOR(index) || rsp >= (uint64_t)frame || !x86_irq_cpu_state() || !empty_private() ||
        !x86_irq_verify(&xis_native.Idt, &xis_native.Idtr, &xp_panic_native.Idt,
            xis_irq_targets, (uint64_t)xis_irq_begin, (uint64_t)xis_irq_end)) xis_native_abort(0x4b34495049454e00ULL);
    uint32_t version = xis_mmio_read(0x30), maximum = (version >> 16) & 255;
    if (!x86_apic_lapic_version(version)) xis_native_abort(0x4b344950494c5600ULL);
    for (uint32_t lvt = 0; lvt <= maximum; ++lvt) {
        uint32_t offset = x86_irq_lvt(maximum, lvt);
        if (!offset) xis_native_abort(0x4b344950494c5601ULL);
        xis_mmio_write(offset, 0x10040);
        if (xis_mmio_read(offset) != 0x10040) xis_native_abort(0x4b344950494c5602ULL);
    }
    x86_exception_load_idt(&xis_native.Idtr);
    X86_KERNEL_IDTR actual;
    x86_exception_read_idt(&actual);
    for (uint32_t byte = 0; byte < sizeof(actual); ++byte)
        if (actual.Bytes[byte] != xis_native.Idtr.Bytes[byte]) xis_native_abort(0x4b34495049494454ULL);
    xis_mmio_write(0x80, 0); xis_mmio_write(0xf0, 0x1ff);
    if (xis_mmio_read(0x80) || xis_mmio_read(0xf0) != 0x1ff || !empty_private() ||
        !xis_ready(index, index)) xis_native_abort(0x4b34495049524459ULL);
    return 1;
}

// ====== xis_native_flush_context ======

// Intel SDM 078US 4.10.4.1: reload the verified CR3 with CR4.PGE7 and PCIDE17 clear.
int xis_native_flush_context(void) {
    const uint64_t incompatible = (1ULL << 7) | (1ULL << 17);
    uint64_t root = __readcr3();
    if (ks_irq_state() || !root || root != xp_panic_native.Root || (__readcr4() & incompatible))
        return 0;
    __writecr3(root);
    return 1;
}

#if KERNEL_TEST_AP_SERVICE == 7
#define XIS_TIMER_VECTOR 0xd1U
#define XIS_TIMER_PERIODIC (1U << 17)
#define XIS_TIMER_MASKED (1U << 16)
uint64_t xis_timer_reload;
uint64_t xis_timer_ticks[64];
static uint8_t timer_state[64];

// ====== timer_command ======

// Intel SDM 078US Vol. 3A §11.5.4: LVT +320h, Initial Count +380h,
// Current Count +390h (RO), Divide +3e0h (3 divides by 16).
static int timer_command(uint32_t index, uint32_t operation)
{
    volatile uint32_t *lvt = (volatile uint32_t *)(X86_LAPIC_VIRTUAL + 0x320);
    volatile uint32_t *initial = (volatile uint32_t *)(X86_LAPIC_VIRTUAL + 0x380);
    volatile const uint32_t *current = (volatile const uint32_t *)(X86_LAPIC_VIRTUAL + 0x390);
    volatile uint32_t *divide = (volatile uint32_t *)(X86_LAPIC_VIRTUAL + 0x3e0);
    uint32_t periodic = XIS_TIMER_PERIODIC | XIS_TIMER_VECTOR;

    if (operation == XIS_ARM_TIMER && !timer_state[index]) {
        uint64_t reload = ks_atomic_load_acquire(&xis_timer_reload);
        if (!reload || reload > UINT32_MAX || *initial || *current || *lvt != 0x10040) return 0;
        *divide = 3;
        *lvt = periodic;
        *initial = (uint32_t)reload;
        if (*divide != 3 || *lvt != periodic || *initial != reload) return 0;
        timer_state[index] = 1;
        return 1;
    }
    if (operation == XIS_STOP_TIMER && timer_state[index] == 1) {
        *lvt = periodic | XIS_TIMER_MASKED;
        *initial = 0;
        if (*lvt != (periodic | XIS_TIMER_MASKED) || *initial || *current) return 0;
        timer_state[index] = 2;
        return 1;
    }
    return 0;
}

// ====== timer_interrupt ======

static void timer_interrupt(uint32_t index)
{
    const uint32_t isr = 0x100 + (XIS_TIMER_VECTOR / 32) * 16;
    const uint32_t bit = 1U << (XIS_TIMER_VECTOR % 32);
    for (uint32_t offset = 0x100; offset <= 0x170; offset += 16)
        if (xis_mmio_read(offset) != (offset == isr ? bit : 0))
            xis_native_abort(0x4b34435449535200ULL);
    uint64_t count = ks_atomic_load_relaxed(&xis_timer_ticks[index]);
    if (!timer_state[index] || count == UINT64_MAX)
        xis_native_abort(0x4b3443545449434bULL);
    xis_mmio_write(0xb0, 0);
    if (xis_mmio_read(isr)) xis_native_abort(0x4b344354454f4900ULL);
    ks_atomic_store_release(&xis_timer_ticks[index], count + 1);
}
#endif

// ====== xis_native_dispatch ======

void xis_native_dispatch(const X86_IRQ_FRAME *frame) {
    uint32_t index = xis_native_index();
    uint64_t top = X86_AP_FRAME(index), address = (uint64_t)frame;
#ifdef KERNEL_TEST_AP_SERVICE
    const uint64_t idle_flags = 0x602;
#else
    const uint64_t idle_flags = 0x202;
#endif
    if (!index || ks_irq_state() || (address & 7) || address < X86_AP_STACK_FLOOR(index) || address > top - sizeof(*frame) ||
        frame->Cs != 8 || frame->Ss != 16 || frame->Rsp != top - XIS_IDLE_FRAME_BYTES ||
        (frame->Flags & 0x7602) != idle_flags || !xp_image_contains(frame->Rip, 1))
        xis_native_abort(0x4b3449504946524dULL);
    XIS_CPU *cpu = &xis_native.Cpus[index];
    if (frame->Vector == 0xff) {
        for (uint32_t word = 0; word < 8; ++word)
            if (xis_mmio_read(0x100 + word * 16)) xis_native_abort(0x4b34495049535052ULL);
        uint64_t previous = ks_atomic_load_relaxed(&cpu->Spurious);
        if (previous == UINT64_MAX) xis_native_abort(0x4b34495049535752ULL);
        ks_atomic_store_release(&cpu->Spurious, previous + 1);
        return;
    }
#if KERNEL_TEST_AP_SERVICE == 7
    if (frame->Vector == XIS_TIMER_VECTOR) {
        timer_interrupt(index);
        return;
    }
#endif
    if (frame->Vector != XIS_VECTOR) xis_native_abort(0x4b34495049564543ULL);
    for (uint32_t word = 0; word < 8; ++word)
        if (xis_mmio_read(0x100 + word * 16) != (word == XIS_VECTOR / 32 ? 1U << (XIS_VECTOR % 32) : 0))
            xis_native_abort(0x4b34495049495352ULL);
    uint64_t generation = xis_claim(index, index);
    if (!generation || generation == XIS_INVALID) xis_native_abort(0x4b34495049434c4dULL);
    ks_atomic_store_release(&cpu->FrameRsp, frame->Rsp);
    ks_atomic_store_release(&cpu->Entered, generation);
#ifdef XIS_REMAP_TEST
    // Diagnostic rounds count from the generation the probe found: boot-time shootdowns come first.
    uint64_t round = generation - ks_atomic_load_acquire(&xis_probe_base);
#endif
#if KERNEL_TEST_AP_SERVICE == 5
    if (index == 1 && ks_atomic_load_acquire(&xis_probe_armed)) {
        uint32_t start, now;
        if (!xis_native_now(NULL, &start)) xis_native_abort(0x4b34495049434c4bULL);
        for (uint32_t poll = 0; poll < 1000000; ++poll) {
            if (!xis_native_now(NULL, &now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
                break;
            ks_pause();
        }
        xis_native_abort(0x4b344950494e4d49ULL);
    }
#endif
    uint32_t operation = xis_operation(index, index, generation);
#if KERNEL_TEST_AP_SERVICE == 7
    if (operation == XIS_ARM_TIMER || operation == XIS_STOP_TIMER) {
        if (!timer_command(index, operation)) xis_native_abort(0x4b344354434d4400ULL);
    } else
#endif
    if (operation != XIS_PING &&
        (operation != XIS_FLUSH_CONTEXT || !xis_native_flush_context()))
        xis_native_abort(0x4b34544c42464c53ULL);
#ifdef XIS_REMAP_TEST
    if (round == XIS_WARM_ROUND || round == XIS_READ_ROUND) {
        const volatile unsigned char *view = (const volatile unsigned char *)X86_KERNEL_PAGE_WINDOW;
        unsigned char expected = round == XIS_WARM_ROUND ? XIS_OLD_VALUE : XIS_NEW_VALUE;
        if (operation != XIS_PING || *view != expected) xis_native_abort(0x4b34544c42524541ULL);
    }
#endif
    xis_mmio_write(0xb0, 0);
    if (xis_mmio_read(0x100 + (XIS_VECTOR / 32) * 16)) xis_native_abort(0x4b34495049454f49ULL);
    ks_atomic_store_release(&cpu->Eois, generation);
    ks_atomic_store_release(&cpu->Delivered, generation);
#if KERNEL_TEST_AP_SERVICE == 2
    if (index == 1 && round == XIS_FLUSH_ROUND) return;
#elif KERNEL_TEST_AP_SERVICE == 3
    if (index == 1 && round == XIS_FLUSH_ROUND) {
        if (!xis_complete(index, index, generation + 1)) xis_native_abort(0x4b34495049424144ULL);
        xis_native_abort(0x4b34544c42424144ULL);
    }
#elif KERNEL_TEST_AP_SERVICE == 6
    // Release diagnostic: AP1 completes EOI for the flush and withholds the ACK.
    if (index == 1 && operation == XIS_FLUSH_CONTEXT && ks_atomic_load_acquire(&xis_release_armed)) return;
#endif
    if (!xis_complete(index, index, generation)) xis_native_abort(0x4b3449504941434bULL);
}

#ifdef KERNEL_TEST_AP_SERVICE
// ====== xis_native_verified ======

void xis_native_verified(const unsigned char *fx) {
    uint32_t index = xis_native_index();
    if (!index || ks_irq_state()) xis_native_abort(0x4b34495049565246ULL);
    uint64_t top = X86_AP_FRAME(index);
    if ((uint64_t)fx != top - 512) xis_native_abort(0x4b34495049565800ULL);
    for (uint32_t byte = 160; byte < 416; ++byte)
        if (fx[byte] != 255) xis_native_abort(0x4b34495049584d4dULL);
    // Intel SDM 253666-089US Vol.2A FXSAVE, Table 3-48: FCW0, FSW2, FTW4, MXCSR24, ST0=32.
    for (uint32_t byte = 32; byte < 39; ++byte)
        if (fx[byte]) xis_native_abort(0x4b34495049463830ULL);
    if (fx[0] != 0x7f || fx[1] != 3 || fx[2] != 0 || fx[3] != 0x38 || fx[4] != 0x80 ||
        fx[24] != 0x80 || fx[25] != 0x1f || fx[26] || fx[27] ||
        fx[32 + 7] != 0x80 || fx[32 + 8] != 0xff || fx[32 + 9] != 0x3f)
        xis_native_abort(0x4b34495049463837ULL);
    ks_atomic_store_release(&xis_native.Cpus[index].Verified, xis_value(index, XIS_ACK));
}
#endif
