#include "notify.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/interrupts/apic/icr/icr.h"
#include "arch/x86/kernel/scheduler/batch/batch.h"
#ifdef KERNEL_TEST_AP_NOTIFY_INFLIGHT
#include "test/kernel/console/video/ap/channel.h"
#endif

XCN_RECORD xcn_record;
static X86_IRQ_TOKEN subscription;
static X86_IRQ_REGISTRY *registry;
static _Alignas(8) volatile uint32_t completion[2];

// ====== quiet_private ======

static int quiet_private(void) {
    return !ks_irq_state() && !xis_native_index() &&
        x86_lapic_idle(xp_panic_native.Lapic);
}

// ====== handle_private ======

static int handle_private(void) {
    if (ks_irq_state() || xis_native_index() || !ks_atomic_load_acquire(&xcn_record.Active) ||
        xcn_record.Self != (uint64_t)(uintptr_t)&xcn_record ||
        !subscription.Id || subscription.Vector != XCN_VECTOR || completion[0] == UINT32_MAX)
        return 0;
    uint64_t sequence = kernel_console_ap_notify(3,0,xcn_record.Token,0,0);
    if (!sequence || sequence == UINT64_MAX) return 0;
#ifndef KERNEL_TEST_AP_NOTIFY_NO_ACK
    if (!kernel_console_ap_notify(4,0,xcn_record.Token,0,sequence)) return 0;
#endif
    if (!kernel_scheduler_irq_post_bound(XCN_EVENT,xcn_record.Self,xcn_record.Binding) ||
        !x86_lapic_eoi_vector(xp_panic_native.Lapic,XCN_VECTOR)) return 0;
    ++xcn_record.Delivered;
    ++xcn_record.Eois;
    ++completion[0];
    return 1;
}

// ====== xcn_prepare ======

int xcn_prepare(uint64_t token) {
    if (ks_irq_state() || xis_native_index() || !token || xcn_record.Self ||
        !x86_lapic_authority(xp_panic_native.Lapic) || !(registry = x86_irq_registry())) return 0;
    xcn_record.Self = (uint64_t)(uintptr_t)&xcn_record;
    xcn_record.Token = token;
    if (!kernel_console_ap_notify(0,0,token,0,0) ||
        !(xcn_record.Binding = kernel_scheduler_irq_bind(XCN_EVENT,xcn_record.Self)) ||
        !x86_irq_registry_bind(registry,XCN_VECTOR,handle_private,quiet_private,&subscription)) return 0;
    ks_atomic_store_release(&xcn_record.Active,1);
    return 1;
}

typedef struct { uint32_t Start, Polls; } XCN_BUDGET;

// ====== budget_private ======

static int budget_private(void *context) {
    XCN_BUDGET *budget = context;
    uint32_t now;
    return ++budget->Polls <= 1000000 && xis_native_now(NULL,&now) &&
        ((now - budget->Start) & xp_panic_native.Source.Mask) < KERNEL_PM_TIMER_HZ;
}

// ====== read_private ======

static int read_private(void *context, uint32_t offset, uint32_t *value) {
    (void)context;
    if (offset != X86_ICR_LOW && offset != X86_ICR_ESR) return 0;
    *value = xis_mmio_read(offset);
    return 1;
}

// ====== write_private ======

// Intel SDM 253668-078US 11.6.1/Fig11-12: ICR310h destination31:24,
// ICR300h fixed delivery10:8=0, physical11=0, assert14=1, edge15=0.
// Only AP1 -> verified BSP ID, vector43h. 11.5.3: ESR280h written with zero.
// Published pinned UC LAPIC mapping outlives all sends; IF0 excludes local reentry.
static int write_private(void *context, uint32_t offset, uint32_t value) {
    (void)context;
    if (ks_irq_state() || xis_native_index() != 1 || !ks_atomic_load_acquire(&xcn_record.Active)) return 0;
    if (offset == X86_ICR_LOW) {
        if (value != (0x4000U | XCN_VECTOR)) return 0;
        return xcn_icr_low(value);
    }
    if ((offset == X86_ICR_HIGH && xp_panic_native.Ids[0] < 255 &&
         value == (xp_panic_native.Ids[0] << 24)) || (offset == X86_ICR_ESR && !value)) {
        xis_mmio_write(offset,value);
        return 1;
    }
    return 0;
}

// ====== pause_private ======

static void pause_private(void *context) { (void)context; ks_pause(); }

// ====== xcn_send ======

int xcn_send(uint32_t producer) {
    if (ks_irq_state() || xis_native_index() != 1 || producer >= 2 || !xat_task_valid() ||
        x86_scheduler_index() != producer || !ks_atomic_load_acquire(&xcn_record.Active)) return 0;
    uint64_t notice = kernel_console_ap_notify(1,1,xcn_record.Token,producer,0);
    if (!notice) return 0;
    if (notice & 1) {
        XCN_BUDGET budget = {0,0};
        if (!xis_native_now(NULL,&budget.Start)) return 0;
        X86_ICR_PORT port = {read_private,write_private,budget_private,pause_private,NULL,&budget};
        if (x86_icr_send(&port,xp_panic_native.Ids[0],0x4000U | XCN_VECTOR,
            X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT) != X86_ICR_OK) return 0;
        ++xcn_record.Sent;
    }
#ifdef KERNEL_TEST_AP_NOTIFY_INFLIGHT
    if (producer == 1 && kca_record.Sent[1] == 1000) return 1;
#endif
    return kernel_console_ap_notify(2,1,xcn_record.Token,producer,notice) == 1;
}

// ====== xcn_poll ======

// Diagnostic bounded IRQ window, NOT an HLT or worker wake proof.
int xcn_poll(void) {
    if (ks_irq_state() || xis_native_index() || !ks_atomic_load_acquire(&xcn_record.Active) ||
        completion[0] == UINT32_MAX) return 0;
    (void)x86_irq_wait(completion,completion[0] + 1,128);
    KERNEL_EVENT_REPLY reply = kernel_scheduler_irq_drain();
    if (reply.reply.status || reply.woken || completion[1]) return 0;
    ++xcn_record.Drains;
    return 1;
}

// ====== xcn_close ======

int xcn_close(int queue_empty) {
    if (ks_irq_state() || xis_native_index() || !ks_atomic_load_acquire(&xcn_record.Active) ||
        xad_collect_allowed(1,0,1,xcn_record.Token) != 1 ||
        !kernel_console_ap_notify(5,0,xcn_record.Token,0,0)) return 0;
    uint32_t start, now;
    if (!xis_native_now(NULL,&start)) return 0;
    int quiet = 0;
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        if (!xcn_poll() || !xis_native_now(NULL,&now)) return 0;
        if (quiet_private() && kernel_console_ap_notify(7,0,xcn_record.Token,3,0) == 0) {
            quiet = 1;
            break;
        }
        if (((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ) return 0;
    }
    for (uint32_t field = 0; field < 6; ++field)
        xcn_record.Snapshot[field] = kernel_console_ap_notify(7,0,xcn_record.Token,field,0);
    if (!quiet || !kernel_console_ap_notify(6,0,xcn_record.Token,0,queue_empty ? 3 : 1)) {
        xcn_record.Denied = 1;
        return 0;
    }
    if (x86_irq_registry_deliveries(registry,&subscription) != xcn_record.Delivered ||
        !xcn_record.Delivered || xcn_record.Delivered != xcn_record.Eois ||
        !x86_irq_registry_unbind(registry,&subscription) ||
        !kernel_scheduler_irq_unbind(XCN_EVENT,xcn_record.Self,xcn_record.Binding)) return 0;
    ks_atomic_store_release(&xcn_record.Active,0);
    xcn_record.Closed = 1;
    return 1;
}

// ====== xcn_release_allowed ======

int xcn_release_allowed(void) {
    return !ks_irq_state() && !xis_native_index() && xcn_record.Closed == 1 &&
        !ks_atomic_load_acquire(&xcn_record.Active) && !subscription.Id &&
        kernel_console_ap_notify(7,0,xcn_record.Token,0,0) == 3;
}
