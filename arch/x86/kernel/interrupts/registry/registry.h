#ifndef OS_X86_IRQ_REGISTRY_H
#define OS_X86_IRQ_REGISTRY_H
#include "arch/x86/kernel/platform.h"
typedef int (*X86_IRQ_CALLBACK)(void);
typedef struct { BOOT_U64 Id; BOOT_U32 Vector, Reserved; } X86_IRQ_TOKEN;
typedef struct {
    BOOT_U64 Id;
    X86_IRQ_CALLBACK Handle, Quiet;
    BOOT_U64 Delivered;
    BOOT_U32 Vector, Fault;
} X86_IRQ_SUBSCRIPTION;
typedef struct {
    BOOT_U64 Self, CodeBegin, CodeEnd, Next;
    BOOT_U32 Busy, Reserved;
    X86_IRQ_SUBSCRIPTION Slots[2];
} X86_IRQ_REGISTRY;
_Static_assert(sizeof(X86_IRQ_REGISTRY) == 120, "Registro IRQ statico");
_Static_assert(sizeof(X86_IRQ_TOKEN) == 16, "Identita subscription IRQ");
BOOT_U32 x86_irq_subscription_fields(BOOT_U32 vector, BOOT_U32 fault, BOOT_U64 id, BOOT_U64 next);
int x86_irq_registry_initialize(X86_IRQ_REGISTRY *registry, BOOT_U64 begin, BOOT_U64 end);
int x86_irq_registry_validate(const X86_IRQ_REGISTRY *registry);
int x86_irq_registry_bind(X86_IRQ_REGISTRY *registry, BOOT_U32 vector,
    X86_IRQ_CALLBACK handle, X86_IRQ_CALLBACK quiet, X86_IRQ_TOKEN *token);
int x86_irq_registry_unbind(X86_IRQ_REGISTRY *registry, X86_IRQ_TOKEN *token);
int x86_irq_registry_deliver(X86_IRQ_REGISTRY *registry, BOOT_U32 vector);
BOOT_U64 x86_irq_registry_deliveries(const X86_IRQ_REGISTRY *registry, const X86_IRQ_TOKEN *token);
X86_IRQ_REGISTRY *x86_irq_registry(void);
int x86_irq_wait(volatile BOOT_U32 *counts, BOOT_U32 goal, BOOT_U32 budget);
// Chiamante ring zero, source Count/Error valida per tutta l'attesa, IF=0.
// Ritorna 1+HLT eseguiti oppure zero; budget di risvegli, non timeout.
BOOT_U32 x86_irq_sleep(volatile BOOT_U32 *counts, BOOT_U32 goal, BOOT_U32 budget);
BOOT_U64 x86_irq_sleep_resume(void);
#endif
