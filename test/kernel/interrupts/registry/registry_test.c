#include "arch/x86/kernel/interrupts/registry/registry.h"
static X86_IRQ_REGISTRY registry, saved;
static BOOT_U32 environment = 1, quiet_result = 1, handle_result = 1, calls;
static X86_IRQ_TOKEN first, second;

// ====== ambiente IF sostituito da una fixture senza privilegi ===== //

BOOT_U64 x86_exception_environment(void) { return environment; }

// ====== callback senza hardware con tentativi di rientro durante la consegna ===== //

static int quiet(void)
{
    if (x86_irq_registry_deliver(&registry,0x40) ||
        x86_irq_registry_unbind(&registry,&first)) return 0;
    return (int)quiet_result;
}

// ====== nessuna registrazione rimozione o consegna annidata in un handler ===== //

static int handler(void)
{
    X86_IRQ_TOKEN output = {0};
    ++calls;
    if (x86_irq_registry_bind(&registry,0x41,handler,quiet,&output) ||
        x86_irq_registry_unbind(&registry,&first) ||
        x86_irq_registry_deliver(&registry,0x40) || x86_irq_registry_token_live(&registry,&first)) return 0;
    return (int)handle_result;
}

// ====== fixture con intervallo callback esplicito e owner vergine ===== //

static int reset(void)
{
    BOOT_U32 byte;
    BOOT_U64 a = (BOOT_U64)handler, b = (BOOT_U64)quiet;
    for (byte = 0; byte < sizeof(registry); ++byte) ((unsigned char *)&registry)[byte] = 0;
    first.Id = second.Id = 0;
    first.Vector = first.Reserved = second.Vector = second.Reserved = 0;
    environment = quiet_result = handle_result = 1; calls = 0;
    return x86_irq_registry_initialize(&registry,a < b ? a : b,(a > b ? a : b)+1);
}

// ====== confronti byte esatti sulle operazioni rifiutate ===== //

static void save(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(registry); ++byte)
        ((unsigned char *)&saved)[byte] = ((unsigned char *)&registry)[byte];
}

// ====== nessuna differenza nei record dopo un rifiuto ===== //

static int unchanged(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(registry); ++byte)
        if (((unsigned char *)&saved)[byte] != ((unsigned char *)&registry)[byte]) return 0;
    return 1;
}

// ====== due subscription indipendenti senza sostituzione di un handler vivo ===== //

static int lifecycle(void)
{
    X86_IRQ_TOKEN other = {0}, stale;
    BOOT_U32 byte;
    if (!reset() || x86_irq_registry_initialize(&registry,4096,8192)) return 1;
    if (!x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) || first.Id != 1 ||
        !x86_irq_registry_bind(&registry,0x41,handler,quiet,&second) || second.Id != 2) return 2;
    save();
    if (!x86_irq_registry_token_live(&registry,&first) || !x86_irq_registry_token_live(&registry,&second)) return 12;
    if (x86_irq_registry_bind(&registry,0x40,handler,quiet,&other) || !unchanged() || other.Id) return 3;
    if (!x86_irq_registry_deliver(&registry,0x40) || calls != 1 ||
        x86_irq_registry_deliveries(&registry,&first) != 1 ||
        x86_irq_registry_deliveries(&registry,&second)) return 4;
    stale = first;
    quiet_result = 0; save();
    if (x86_irq_registry_unbind(&registry,&first) || !unchanged() || first.Id != stale.Id) return 5;
    quiet_result = 1;
    if (!x86_irq_registry_unbind(&registry,&first) || first.Id || first.Vector || first.Reserved) return 6;
    if (x86_irq_registry_unbind(&registry,&stale) || x86_irq_registry_deliver(&registry,0x40) ||
        x86_irq_registry_token_live(&registry,&stale)) return 7;
    if (!x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) || first.Id != 3 ||
        x86_irq_registry_unbind(&registry,&stale) || x86_irq_registry_token_live(&registry,&stale) ||
        !x86_irq_registry_token_live(&registry,&first)) return 8;
    if (!x86_irq_registry_unbind(&registry,&first) || !x86_irq_registry_unbind(&registry,&second)) return 9;
    for (byte = 0; byte < sizeof(registry.Slots); ++byte)
        if (((unsigned char *)registry.Slots)[byte]) return 10;
    return x86_irq_registry_validate(&registry) ? 0 : 11;
}

// ====== errore handler persistente senza contare una consegna riuscita ===== //

static int failure(void)
{
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first)) return 1;
    handle_result = 0;
    if (x86_irq_registry_deliver(&registry,0x40) || calls != 1 ||
        x86_irq_registry_deliveries(&registry,&first) || registry.Slots[0].Fault != 1 || registry.Busy ||
        x86_irq_registry_token_live(&registry,&first)) return 2;
    handle_result = 1;
    if (x86_irq_registry_deliver(&registry,0x40) || calls != 1) return 3;
    quiet_result = 0;
    if (x86_irq_registry_unbind(&registry,&first)) return 4;
    quiet_result = 1;
    return x86_irq_registry_unbind(&registry,&first) ? 0 : 5;
}

// ====== IF alias token e callback fuori codice impediscono mutazioni ===== //

static int invalid(void)
{
    X86_IRQ_TOKEN output = {0};
    BOOT_U32 vector;
    if (!reset()) return 1;
    save();
    for (vector = 0; vector < 256; ++vector) {
        if ((vector >= 0x40 && vector <= 0x43) || vector == X86_PREEMPT_VECTOR) continue;
        if (x86_irq_registry_bind(&registry,vector,handler,quiet,&output) || !unchanged()) return 2;
    }
    if (x86_irq_registry_bind(&registry,0x40,0,quiet,&output) ||
        x86_irq_registry_bind(&registry,0x40,handler,(X86_IRQ_CALLBACK)registry.CodeEnd,&output) ||
        x86_irq_registry_bind(&registry,0x40,handler,quiet,(X86_IRQ_TOKEN *)&registry) ||
        x86_irq_registry_bind(&registry,0x40,handler,quiet,0) || !unchanged()) return 3;
    quiet_result = 0;
    if (x86_irq_registry_bind(&registry,0x40,handler,quiet,&output) || !unchanged()) return 4;
    quiet_result = 1; environment = 0;
    if (x86_irq_registry_bind(&registry,0x40,handler,quiet,&output) || !unchanged()) return 5;
    environment = 1;
    if (!x86_irq_registry_bind(&registry,0x40,handler,quiet,&first)) return 6;
    save(); environment = 0;
    if (x86_irq_registry_deliver(&registry,0x40) || x86_irq_registry_unbind(&registry,&first) ||
        x86_irq_registry_deliveries(&registry,&first) || !unchanged()) return 7;
    environment = 1;
    return 0;
}

// ====== metadati corrotti owner spostato e slot inattivi non sono accettati ===== //

static int corruption(void)
{
    X86_IRQ_REGISTRY moved;
    BOOT_U32 bit;
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first)) return 1;
    moved = registry;
    if (x86_irq_registry_validate(&moved)) return 2;
    for (bit = 0; bit < 64; ++bit) {
        registry.Self ^= 1ULL<<bit;
        if (x86_irq_registry_validate(&registry)) return 3;
        registry.Self ^= 1ULL<<bit;
    }
    for (bit = 0; bit < 32; ++bit) {
        registry.Reserved = 1U<<bit;
        if (x86_irq_registry_validate(&registry)) return 4;
    }
    registry.Reserved = 0;
    registry.Slots[1].Delivered = 1;
    if (x86_irq_registry_validate(&registry)) return 5;
    registry.Slots[1].Delivered = 0;
    registry.Slots[0].Fault = 2;
    if (x86_irq_registry_validate(&registry)) return 6;
    return 0;
}

// ====== mille cicli non autorizzano token vecchi o wrap dei contatori ===== //

static int generations(void)
{
    X86_IRQ_TOKEN stale;
    BOOT_U32 cycle;
    if (!reset()) return 1;
    for (cycle = 0; cycle < 1000; ++cycle) {
        if (!x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) || first.Id != cycle+1) return 2;
        stale = first;
        if (!x86_irq_registry_deliver(&registry,0x40) || !x86_irq_registry_unbind(&registry,&first) ||
            x86_irq_registry_unbind(&registry,&stale)) return 3;
    }
    registry.Next = ~0ULL;
    save();
    if (x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) || !unchanged()) return 4;
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first)) return 5;
    registry.Slots[0].Delivered = ~0ULL;
    if (x86_irq_registry_deliver(&registry,0x40) || calls) return 6;
    return 0;
}

// ====== token alterato e output disallineato non rimuovono la subscription ===== //

static int tokens(void)
{
    X86_IRQ_TOKEN bad;
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first)) return 1;
    bad = first; bad.Reserved = 1;
    save();
    if (x86_irq_registry_unbind(&registry,&bad) || !unchanged()) return 2;
    bad = first; bad.Vector = 0x41;
    if (x86_irq_registry_unbind(&registry,&bad) || !unchanged()) return 3;
    if (x86_irq_registry_unbind(&registry,(X86_IRQ_TOKEN *)((unsigned char *)&first+1)) || !unchanged()) return 4;
    return x86_irq_registry_unbind(&registry,&first) ? 0 : 5;
}

// ====== three_simultaneous_subscriptions ======

static int three_subscriptions(void)
{
    X86_IRQ_TOKEN third = {0}, stale;
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) ||
        !x86_irq_registry_bind(&registry,0x41,handler,quiet,&second) ||
        !x86_irq_registry_bind(&registry,0x42,handler,quiet,&third)) return 1;
    if (third.Id != 3 || !x86_irq_registry_deliver(&registry,0x42) ||
        x86_irq_registry_deliveries(&registry,&third) != 1 ||
        x86_irq_registry_deliveries(&registry,&first) || x86_irq_registry_deliveries(&registry,&second)) return 2;
    registry.Slots[2].Id = first.Id;
    if (x86_irq_registry_validate(&registry)) return 3;
    registry.Slots[2].Id = third.Id; stale = third;
    if (!x86_irq_registry_unbind(&registry,&third) || x86_irq_registry_token_live(&registry,&stale) ||
        !x86_irq_registry_bind(&registry,0x42,handler,quiet,&third) || third.Id != 4 ||
        x86_irq_registry_unbind(&registry,&stale)) return 4;
    if (!x86_irq_registry_unbind(&registry,&third) || !x86_irq_registry_unbind(&registry,&second) ||
        !x86_irq_registry_unbind(&registry,&first)) return 5;
    return x86_irq_registry_validate(&registry) ? 0 : 6;
}

// ====== fourth_subscription_lifetime ======

static int fourth_subscription(void)
{
    X86_IRQ_TOKEN third = {0}, fourth = {0}, stale;
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) ||
        !x86_irq_registry_bind(&registry,0x41,handler,quiet,&second) ||
        !x86_irq_registry_bind(&registry,0x42,handler,quiet,&third) ||
        !x86_irq_registry_bind(&registry,0x43,handler,quiet,&fourth)) return 1;
    if (fourth.Id != 4 || !x86_irq_registry_deliver(&registry,0x43) ||
        x86_irq_registry_deliveries(&registry,&fourth) != 1 ||
        x86_irq_registry_deliveries(&registry,&third)) return 2;
    registry.Slots[3].Id = first.Id;
    if (x86_irq_registry_validate(&registry)) return 3;
    registry.Slots[3].Id = fourth.Id; stale = fourth;
    if (!x86_irq_registry_unbind(&registry,&fourth) || x86_irq_registry_token_live(&registry,&stale) ||
        !x86_irq_registry_bind(&registry,0x43,handler,quiet,&fourth) || fourth.Id != 5 ||
        x86_irq_registry_unbind(&registry,&stale)) return 4;
    if (!x86_irq_registry_unbind(&registry,&fourth) || !x86_irq_registry_unbind(&registry,&third) ||
        !x86_irq_registry_unbind(&registry,&second) || !x86_irq_registry_unbind(&registry,&first)) return 5;
    if (x86_irq_registry_deliver(&registry,0x43)) return 6;
    return x86_irq_registry_validate(&registry) ? 0 : 7;
}

// ====== timer_subscription ======

static int timer_subscription(void)
{
    X86_IRQ_TOKEN third = {0}, fourth = {0}, timer = {0}, stale = {0};
    if (!reset() || !x86_irq_registry_bind(&registry,0x40,handler,quiet,&first) ||
        !x86_irq_registry_bind(&registry,0x41,handler,quiet,&second) ||
        !x86_irq_registry_bind(&registry,0x42,handler,quiet,&third) ||
        !x86_irq_registry_bind(&registry,0x43,handler,quiet,&fourth)) return 1;
    if (!x86_irq_registry_bind(&registry,X86_PREEMPT_VECTOR,handler,quiet,&timer) ||
        timer.Id != 5 || timer.Vector != X86_PREEMPT_VECTOR ||
        registry.Slots[X86_IRQ_TIMER_SLOT].Id != timer.Id) return 2;
    save();
    if (x86_irq_registry_bind(&registry,X86_PREEMPT_VECTOR,handler,quiet,&stale) || !unchanged()) return 3;
    calls = 0;
    if (!x86_irq_registry_deliver(&registry,X86_PREEMPT_VECTOR) || calls != 1 ||
        x86_irq_registry_deliveries(&registry,&timer) != 1 ||
        x86_irq_registry_deliveries(&registry,&fourth)) return 4;
    registry.Slots[X86_IRQ_TIMER_SLOT].Vector = 0x43;
    if (x86_irq_registry_validate(&registry)) return 5;
    registry.Slots[X86_IRQ_TIMER_SLOT].Vector = X86_PREEMPT_VECTOR;
    stale = timer; stale.Vector = 0x43;
    if (x86_irq_registry_unbind(&registry,&stale) || !x86_irq_registry_token_live(&registry,&timer)) return 6;
    if (!x86_irq_registry_unbind(&registry,&timer) || x86_irq_registry_deliver(&registry,X86_PREEMPT_VECTOR) ||
        !x86_irq_registry_token_live(&registry,&fourth)) return 7;
    if (!x86_irq_registry_unbind(&registry,&fourth) || !x86_irq_registry_unbind(&registry,&third) ||
        !x86_irq_registry_unbind(&registry,&second) || !x86_irq_registry_unbind(&registry,&first)) return 8;
    return x86_irq_registry_validate(&registry) ? 0 : 9;
}

// ====== gruppi di verifiche con esito diagnostico distinto ===== //

__declspec(dllexport) int run_irq_registry_tests(void)
{
    int result = lifecycle();
    if (result) return 100+result;
    result = failure(); if (result) return 200+result;
    result = invalid(); if (result) return 300+result;
    result = corruption(); if (result) return 400+result;
    result = generations(); if (result) return 500+result;
    result = tokens(); if (result) return 600+result;
    result = three_subscriptions(); if (result) return 700+result;
    result = fourth_subscription(); if (result) return 800+result;
    result = timer_subscription(); if (result) return 900+result;
    return 0;
}
