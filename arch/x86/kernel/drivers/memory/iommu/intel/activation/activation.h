#ifndef X86_VTD_ACTIVATION_H
#define X86_VTD_ACTIVATION_H
#include "../invalidation/qi.h"
#include "../routing/routing.h"
// Intel VT-d5.0 11.4.4.2 GSTS: TES31, RTPS30, QIES26.
#define X86_VTD_GSTS_TES (1U<<31)
#define X86_VTD_GSTS_RTPS (1U<<30)
// 11.4.4.1 GCMD: TE31, SRTP30 (one command per write).
#define X86_VTD_GCMD_TE (1U<<31)
#define X86_VTD_GCMD_SRTP (1U<<30)
#define X86_VTD_FAULT_LOG 16U
#define X86_VTD_ACT_READY 1U
#define X86_VTD_ACT_ROOT_WRITTEN 2U
#define X86_VTD_ACT_ROOT_PUBLISHED 3U
#define X86_VTD_ACT_INVALIDATED 4U
#define X86_VTD_ACT_TRANSLATING 5U
#define X86_VTD_ACT_DRAINED 6U
#define X86_VTD_ACT_FLUSHED 7U
#define X86_VTD_ACT_UNPUBLISHED 8U
#define X86_VTD_ACT_RETAINED 9U

typedef struct {
    BOOT_U64  Address;
    BOOT_U32  Source, Reason, Read, Index;
} X86_VTD_FAULT;

typedef struct {
    BOOT_U64  Self, Root;
    X86_QI  *Qi;
    X86_VTD_DOMAIN  *Domain;
    BOOT_U32  State, Source, FaultOffset, FaultCount, Failure, Owned, Blocked, Overflows, Logged, Scans, Acknowledged, Spare;
    X86_VTD_FAULT  Log[X86_VTD_FAULT_LOG];
} X86_VTD_ACTIVATION;
_Static_assert(sizeof(X86_VTD_FAULT)==24 && sizeof(X86_VTD_ACTIVATION)==464,"VT-d activation ABI");

BOOT_U64 x86_vtd_activation_command(BOOT_U32 status,BOOT_U32 bit,BOOT_U32 set);
BOOT_U32 x86_vtd_activation_allowed(BOOT_U32 offset,BOOT_U32 width,BOOT_U32 write,BOOT_U64 value,BOOT_U64 root,BOOT_U32 fro,BOOT_U32 nfr);
BOOT_U64 x86_vtd_fault_decode(BOOT_U64 high);
BOOT_U64 x86_vtd_activation_read(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width);
int x86_vtd_activation_write(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value);
int x86_vtd_activation_valid(const X86_VTD_ACTIVATION *owner);

// ====== activation_contract ======

// Private BSP/IF=0 diagnostic API. Caller owns a started idle QI on the single writable unit, a
// VERIFIED_PRIVATE domain and the route selected for it; all three outlive the activation.
// Every failure moves to RETAINED: tables, queue and mapping stay owned because hardware may
// still read them. Faults from other source IDs are blocked DMA, not errors (deny by default).
int x86_vtd_activation_begin(X86_QI *qi,X86_VTD_DOMAIN *domain,const X86_VTD_ROUTE *route,X86_VTD_ACTIVATION *owner);
int x86_vtd_activation_enable(X86_VTD_ACTIVATION *owner);
int x86_vtd_activation_faults(X86_VTD_ACTIVATION *owner);
// Diagnostic DMA tests only: accepts exactly one new fault of the owned requester with this reason,
// request type (1=read) and page address, then clears the owned count. Anything else fails.
int x86_vtd_activation_expect(X86_VTD_ACTIVATION *owner,BOOT_U32 reason,BOOT_U32 read,BOOT_U64 address);
int x86_vtd_activation_disable(X86_VTD_ACTIVATION *owner);
int x86_vtd_activation_end(X86_VTD_ACTIVATION *owner);
#endif
