#ifndef OS_X86_SERVICE_NATIVE_H
#define OS_X86_SERVICE_NATIVE_H
#include "../service.h"
#include "../../panic/native/runtime.h"
#include "arch/x86/kernel/interrupts/return/table.h"

#if KERNEL_TEST_AP_SERVICE >= 1 && KERNEL_TEST_AP_SERVICE <= 3
#define XIS_REMAP_TEST 1
#define XIS_WARM_ROUND 98U
#define XIS_FLUSH_ROUND 99U
#define XIS_READ_ROUND 100U
#define XIS_OLD_VALUE 0x31U
#define XIS_NEW_VALUE 0xc7U
#endif

#ifdef KERNEL_TEST_AP_SERVICE
// Mailbox generation every AP had when the probe started.
extern uint64_t xis_probe_base;
// Set once the probe recorded the base: boot-time shootdowns before it are ordinary flushes.
extern uint64_t xis_probe_armed;
#endif

#if KERNEL_TEST_AP_SERVICE == 6
#define XIS_RELEASE_VALUE 0x5aU
// Set by the probe just before its release, so earlier boot-time flushes are acknowledged.
extern uint64_t xis_release_armed;
#endif

#if KERNEL_TEST_AP_SERVICE == 7
extern uint64_t xis_timer_reload;
extern uint64_t xis_timer_ticks[64];
uint32_t x86_timer_diagnostic_reload(void);
#endif

#define XIS_VECTOR 0xe1U
// Stack bytes reserved by xis_service_loop (idle.asm: sub rsp, 576) below the private frame.
#define XIS_IDLE_FRAME_BYTES 576ULL
uint32_t xis_idle_return_valid(uint64_t frame, uint64_t top, uint64_t rsp, uint64_t cs,
    uint64_t flags, uint64_t ss, uint32_t diagnostic);
uint32_t xis_task_return_valid(uint64_t frame, uint64_t rsp, uint64_t cs, uint64_t flags, uint64_t ss);
typedef struct {
    _Alignas(64) uint64_t Delivered;
    uint64_t  Verified, Spurious, FrameRsp, Eois, Entered, Reserved[2];
} XIS_CPU;
typedef struct {
    X86_IRQ_IDT  Idt;
    X86_KERNEL_IDTR  Idtr;
    unsigned char  Padding[6];
    uint32_t  Count, Reserved;
    uint64_t  Published;
    uint64_t  Shootdowns;
    uint64_t  HeaderReserved[3];
    XIS_CPU  Cpus[64];
} XIS_NATIVE;
extern XIS_NATIVE xis_native;
extern const BOOT_U64 xis_irq_targets[224];
extern const unsigned char xis_irq_begin[], xis_irq_end[];
int xis_native_prepare(void);
uint32_t xis_native_enter(void *frame);
int xis_native_flush_context(void);
void xis_native_dispatch(const X86_IRQ_FRAME *frame);
void xis_native_verified(const unsigned char *fx);
int xis_native_shootdown(void);
uint64_t xis_native_shootdowns(void);
int xis_native_send(uint32_t index);
int xis_native_now(void *context, uint32_t *now);
int xis_native_wait(uint32_t index, uint32_t field, uint64_t value);
uint32_t xis_native_index(void);
uint32_t xis_mmio_read(uint32_t offset);
void xis_mmio_write(uint32_t offset, uint32_t value);
__declspec(noreturn) void xis_native_abort(uint64_t reason);
void xis_service_loop(void);
_Static_assert(sizeof(XIS_CPU) == 64 && _Alignof(XIS_CPU) == 64, "IPI per-CPU counters");
_Static_assert(offsetof(XIS_NATIVE, Cpus) == 4160 && sizeof(XIS_NATIVE) == 8256, "IPI diagnostic snapshot ABI");
#endif
