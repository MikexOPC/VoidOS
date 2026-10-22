#ifndef X86_VTD_QI_H
#define X86_VTD_QI_H
#include "../domains/domain.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#define X86_QI_ENABLE (1U<<26)
typedef struct {
    BOOT_U64  Self, Budget, Cap;
    X86_VTD_MAPPING  *Mapping;
    X86_PM_CLOCK  Clock;
    KERNEL_PHYSICAL_LEASE  Pages[2];
    BOOT_U32  Allocated, Pins, State, EnableIssued, Pending, Completed, Configured;
    BOOT_U32  Tail, Token, PhysicalBits, IotlbOffset, Failure, Status;
} X86_QI;
// Status: GSTS TES31/RTPS30 owned by the activation module; QI accepts them beside QIES26.
#define X86_QI_FOREIGN ((1U<<31)|(1U<<30))
BOOT_U32 x86_qi_cache_line(BOOT_U32 edx,BOOT_U32 ebx);
BOOT_U64 x86_qi_descriptor(BOOT_U32 kind,BOOT_U32 high,BOOT_U32 token,BOOT_U64 status,BOOT_U64 cap);
BOOT_U32 x86_qi_register_allowed(BOOT_U32 offset,BOOT_U32 width,BOOT_U32 write,BOOT_U64 value,BOOT_U64 queue,BOOT_U32 iro);
int x86_qi_prepare(X86_VTD_MAPPING *mapping,const X86_PM_CLOCK *clock,BOOT_U32 bits,X86_QI *owner);
int x86_qi_start(X86_QI *owner);
int x86_qi_submit(X86_QI *owner);
int x86_qi_finish(X86_QI *owner);
int x86_qi_stop(X86_QI *owner);
int x86_qi_valid(const X86_QI *owner);
int x86_qi_status_allow(X86_QI *owner,BOOT_U32 bits);
int x86_qi_poll(X86_QI *owner,BOOT_U32 kind);
int x86_qi_memory(X86_QI *owner,BOOT_U32 action,BOOT_U32 *status);
int x86_qi_clean_domain(X86_VTD_DOMAIN *domain);
BOOT_U64 x86_qi_read(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width);
int x86_qi_write(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value);
BOOT_U64 x86_qi_ticks(const X86_QI *owner);
BOOT_U32 x86_qi_cpu_line(void);
int x86_qi_clean_page(void);
#endif
