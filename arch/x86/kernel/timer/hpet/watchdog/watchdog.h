#ifndef X86_HPET_WATCHDOG_H
#define X86_HPET_WATCHDOG_H
#include "../description/probe/probe.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#define X86_WATCHDOG_EVENT 1U
#define X86_WATCHDOG_TIMEOUT 2U
#define X86_WATCHDOG_FAULT 3U
#define X86_WATCHDOG_STOP 4U
#define X86_WATCHDOG_IDLE 5U
/* Stable diagnostic phases, not a claim about the underlying hardware cause. */
#define X86_WATCHDOG_ENVIRONMENT 1U
#define X86_WATCHDOG_PROFILE 2U
#define X86_WATCHDOG_CAPTURE 3U
#define X86_WATCHDOG_ROUTE 4U
#define X86_WATCHDOG_SELECTION 5U
#define X86_WATCHDOG_CONFIGURE 6U
#define X86_WATCHDOG_BIND 7U
#define X86_WATCHDOG_UNMASK 8U
#define X86_WATCHDOG_OUTPUT 9U
#define X86_WATCHDOG_WAIT_STATE 10U
#define X86_WATCHDOG_SOURCE 11U
#define X86_WATCHDOG_ARM 12U
#define X86_WATCHDOG_IRQ 13U
#define X86_WATCHDOG_WAKE 14U
#define X86_WATCHDOG_CLASSIFY 15U
#define X86_WATCHDOG_DEADLINE 16U
#define X86_WATCHDOG_CLOSE_STATE 17U
#define X86_WATCHDOG_MASK 18U
#define X86_WATCHDOG_UNBIND 19U
#define X86_WATCHDOG_RESTORE_DEVICE 20U
#define X86_WATCHDOG_RESTORE_ROUTE 21U
#define X86_WATCHDOG_VERIFY 22U
#define X86_WATCHDOG_LOG 23U
#define X86_WATCHDOG_NOT_STARTED 0U
#define X86_WATCHDOG_REJECTED 1U
#define X86_WATCHDOG_RUNNING 2U
#define X86_WATCHDOG_STOP_REQUIRED 3U
#define X86_WATCHDOG_RETRY_CANDIDATE 4U
#define X86_WATCHDOG_RETAIN 5U
#define X86_WATCHDOG_RELEASED 6U
#define X86_WATCHDOG_EXHAUSTED 7U
#define X86_WATCHDOG_RECOVERY_LIMIT 3U

typedef struct {
    const KERNEL_BOOT_CONTEXT     *Context;
    X86_PAGE_WINDOW               *Window;
    const X86_PAGING_INVENTORY    *Inventory;
    const X86_LAPIC_MAPPING       *Mapping;
    const KERNEL_MADT_TOPOLOGY    *Topology;
    const X86_APIC_PROFILE        *Profile;
    const X86_APIC_CAPABILITIES   *Capabilities;
    const X86_HPET_DESCRIPTION    *Description;
} X86_WATCHDOG_ENV;

/* Component-private, persistent BSP owner. No copied owner is accepted.
 * Env pointees are trusted, immutable kernel objects through close. */
typedef struct {
    volatile BOOT_U32  Count, Error;
    BOOT_U64           Self;
    X86_WATCHDOG_ENV   Env;
    X86_IRQ_TOKEN      Token;
    BOOT_U64           Physical, Cache, Budget, IoAddress, CounterSaved;
    BOOT_U32           Id, Period, Timer, ConfigSaved, MatchLow, MatchHigh;
    BOOT_U32           GlobalSaved, ConfigBase, IoId, IoVersion, Pin, IoLow, IoHigh;
    BOOT_U32           Start, Target, Ticks, Armed, Acquired, RouteOwned, Ready, Result, Closed;
} X86_HPET_WATCHDOG;
_Static_assert(offsetof(X86_HPET_WATCHDOG, Error) == 4, "IRQ wait prefix");
_Static_assert(offsetof(X86_HPET_WATCHDOG, Token) == 80, "Monitor token ABI");
_Static_assert(offsetof(X86_HPET_WATCHDOG, Armed) == 200, "Monitor state ABI");
_Static_assert(sizeof(X86_HPET_WATCHDOG) == 224, "Monitor owner ABI");

int x86_watchdog_open(const X86_WATCHDOG_ENV *env);
BOOT_U32 x86_watchdog_wait(const volatile BOOT_U32 *source, BOOT_U32 goal,
    const volatile BOOT_U32 *stop);
/* Input dispatcher: acknowledged watchdog deadline without input is normal.
   source is an owned aligned Count/Error prefix with Count in 0..1. */
BOOT_U32 x86_watchdog_idle(const volatile BOOT_U32 *source);
BOOT_U32 x86_watchdog_result(void);
BOOT_U32 x86_watchdog_wake_detail(void);
BOOT_U32 x86_watchdog_arm_detail(void);
BOOT_U64 x86_watchdog_arm_registers(void);
int x86_watchdog_close(void);
/* BSP/IF=0 session history: first operational and first cleanup failure.
 * Reset only when opening an unowned session, not on retries or close. */
BOOT_U32 x86_watchdog_failure(void);
BOOT_U32 x86_watchdog_cleanup_failure(void);
/* Passive decision, then one explicit bounded cleanup attempt. No timer restart. */
BOOT_U32 x86_watchdog_recovery_status(void);
BOOT_U32 x86_watchdog_recover(void);
BOOT_U32 x86_watchdog_recovery_attempts(void);
/* Internal operations, IF=0, no arbitrary physical-address input. */
int x86_watchdog_recovery_claim(void);
/* Initial close or already validated recovery only; component-private backend. */
int x86_watchdog_finish_cleanup(void);
BOOT_U32 x86_watchdog_recovery_policy(BOOT_U32 flags, BOOT_U32 failure,
    BOOT_U32 cleanup, BOOT_U32 attempts);
int x86_watchdog_fail(BOOT_U32 phase);
X86_HPET_WATCHDOG *x86_watchdog_state(void);
int x86_watchdog_valid(void);
/* Scheduler integration only: BSP/IF=0, exactly two stable pinned 16-page leases.
 * End before either lease is released; failure retains the scope and its owners.
 * No device access or budget rebasing. Typed kernel-owned inputs, not user pointers. */
int x86_watchdog_budget_begin(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second);
/* Same invariants; optional third distinct pinned16-page lease for task2.
   NULL third preserves the existing exact32-page gate; non-NULL requires48. */
int x86_watchdog_budget_begin_three(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second,
    const KERNEL_PHYSICAL_LEASE *third);
int x86_watchdog_budget_begin_four(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second,
    const KERNEL_PHYSICAL_LEASE *third, const KERNEL_PHYSICAL_LEASE *fourth);
int x86_watchdog_budget_end(void);
int x86_watchdog_budget_valid(void);
int x86_watchdog_budget_active(void);
int x86_watchdog_device(BOOT_U32 operation);
int x86_watchdog_route(BOOT_U32 operation);
/* Select next comparator before any hardware mutation; routes are scalar Rust policy. */
int x86_watchdog_select(BOOT_U32 first);
BOOT_U32 x86_watchdog_route_mask(BOOT_U32 timer, BOOT_U32 count, BOOT_U32 routes);
int x86_watchdog_mmio_read(BOOT_U32 offset, BOOT_U32 *value);
int x86_watchdog_mmio_write(BOOT_U32 offset, BOOT_U32 value);
BOOT_U32 x86_watchdog_ticks(BOOT_U32 period, BOOT_U32 milliseconds);
BOOT_U32 x86_watchdog_ahead(BOOT_U32 start, BOOT_U32 now, BOOT_U32 ticks);
BOOT_U32 x86_watchdog_classify(BOOT_U32 count, BOOT_U32 error, BOOT_U32 goal, BOOT_U32 stop);
#endif
