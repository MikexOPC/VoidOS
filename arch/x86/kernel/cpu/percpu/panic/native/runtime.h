#ifndef OS_X86_PANIC_RUNTIME_H
#define OS_X86_PANIC_RUNTIME_H
#include "../transport/transport.h"
#include "../output/output.h"
#include "../../internal.h"
#include "../../resources/resources.h"
#include "../../clock/clock.h"
#include "../../identity/identity.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/kernel/exceptions/terminal.h"
typedef struct {
    uint32_t Count, Ids[64];
    const X86_CPU_LOCAL *Contexts[64];
    XP_PANIC_RECORD *Records[64];
    X86_PM_CLOCK Source;
    const X86_LAPIC_MAPPING *Lapic;
    uint64_t Root, Pat;
    X86_GDT_POINTER BspGdtr;
    XP_PANIC_TRANSPORT Transport;
    XP_PANIC_SINK Sink;
    X86_KERNEL_IDT Idt;
    X86_KERNEL_IDTR Idtr, BspIdtr;
} XP_PANIC_NATIVE;
typedef struct { uint64_t Saved[15]; X86_EXCEPTION_FRAME Frame; } XP_PANIC_RAW;
typedef struct { uint64_t Saved[15], Flags, Return; } XP_PANIC_CALL;
_Static_assert(sizeof(XP_PANIC_RAW) == 176 && offsetof(XP_PANIC_RAW, Frame) == 120, "Assembly fault capture ABI");
_Static_assert(sizeof(XP_PANIC_CALL) == 136 && offsetof(XP_PANIC_CALL, Return) == 128, "Assembly voluntary capture ABI");
extern XP_PANIC_NATIVE xp_panic_native;
extern uint64_t xp_panic_armed, xp_panic_output, xp_panic_recursion;
int xp_panic_native_prepare(const X86_LAPIC_MAPPING *lapic);
int xp_panic_lapic_held(const X86_LAPIC_MAPPING *lapic);
int xp_panic_native_admit(uint32_t index);
uint32_t xp_panic_apic_id(void);
int xp_panic_hw_read(void *context, uint32_t offset, uint32_t *value);
int xp_panic_hw_write(void *context, uint32_t offset, uint32_t value);
int xp_panic_hw_now(void *context, uint32_t *value);
void xp_panic_hw_pause(void *context);
uint8_t xp_panic_hw_port_read(void *context, uint16_t port);
int xp_panic_hw_port_write(void *context, uint16_t port, uint8_t value);
void xp_panic_report(uint32_t index);
__declspec(noreturn) void xp_panic_fault(const XP_PANIC_RAW *raw, uint64_t cr2, uint64_t cr3);
__declspec(noreturn) void xp_panic_call(const XP_PANIC_CALL *raw, uint64_t cr2, uint64_t cr3, uint32_t selectors);
__declspec(noreturn) void xp_panic_voluntary(uint64_t reason);
__declspec(noreturn) void xp_panic_quiet_stop(uint64_t marker);
#endif
