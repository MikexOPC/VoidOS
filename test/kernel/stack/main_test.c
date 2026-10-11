#include "arch/x86/kernel/stack/main.h"
static X86_KERNEL_MAIN_STACK main_stack;
static BOOT_START_INFO info;
static BOOT_U64 current_rsp;
KERNEL_PHYSICAL_ALLOCATOR *x86_test_stack_pool(void);
void x86_test_stack_owner(X86_KERNEL_STACKS *owner);
int x86_test_stack_fixture(void);
BOOT_U32 x86_test_stack_invalidations(void);
BOOT_U32 x86_test_stack_ordering(void);
void x86_test_stack_missing(BOOT_U32 value);

// ====== RSP simulato e nessun cambio reale dello stack host ===== //

BOOT_U64 x86_kernel_stack_pointer(void) { return current_rsp; }

// ====== il backend privilegiato non deve essere eseguito dai test host ===== //

__declspec(noreturn) void x86_kernel_stack_switch(BOOT_U64 top, void (*next)(void))
{
    (void)top; (void)next;
    for (;;) { }
}

// ====== il vecchio backing e riservato fuori dal pool WB del kernel ===== //

static int fixture(void)
{
    BOOT_U32 byte, slot;
    volatile BOOT_U64 *entries;
    if (!x86_test_stack_fixture()) return 0;
    for (byte = 0; byte < sizeof(main_stack); ++byte) ((volatile unsigned char *)&main_stack)[byte] = 0;
    for (byte = 0; byte < sizeof(info); ++byte) ((volatile unsigned char *)&info)[byte] = 0;
    info.StackBase = X86_BOOT_STACK_VIRTUAL;
    info.StackBytes = 65536;
    info.PageTableRoot = 0x1000;
    entries = x86_stack_entries(3);
    for (slot = 1; slot <= 16; ++slot) entries[slot] = (0x2000000ULL + (slot - 1)*4096) | 3 | (1ULL << 63);
    current_rsp = X86_BOOT_STACK_TOP - 256;
    x86_test_stack_owner(&main_stack.Storage);
    return 1;
}

// ====== acquisizione zeroed rollback e contatori esatti ===== //

static int lifecycle_tests(void)
{
    BOOT_U32 byte;
    KERNEL_PHYSICAL_ALLOCATOR *pool = x86_test_stack_pool();
    if (!fixture() || !x86_kernel_main_prepare(&main_stack, pool, &info) ||
        pool->FreePages != 16 || x86_test_stack_invalidations() != 16) return 1;
    for (byte = 0; byte < 65536; ++byte) if (x86_stack_bytes(4)[byte]) return 2;
    if (!x86_kernel_main_release(&main_stack) || pool->FreePages != 32 ||
        x86_test_stack_invalidations() != 32 || x86_test_stack_ordering() ||
        x86_kernel_main_release(&main_stack)) return 3;
    if (!x86_kernel_main_prepare(&main_stack, pool, &info) ||
        !x86_kernel_main_commit(&main_stack) || x86_kernel_main_release(&main_stack) ||
        x86_kernel_main_retire(&main_stack)) return 4;
    current_rsp = X86_KERNEL_MAIN_TOP - 256;
    if (!x86_kernel_main_retire(&main_stack) || main_stack.Phase != 3 ||
        x86_test_stack_invalidations() != 64 || x86_test_stack_ordering() ||
        pool->FreePages != 16 || !kernel_physical_is_pinned(pool, &main_stack.Storage.Lease)) return 5;
    if (x86_kernel_main_retire(&main_stack) || x86_kernel_main_commit(&main_stack)) return 6;
    return 0;
}

// ====== vietato revocare un mapping loader alterato dopo il cambio ===== //

static int retirement_tests(void)
{
    BOOT_U32 slot, bit;
    volatile BOOT_U64 *entries;
    if (!fixture() || !x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info) ||
        !x86_kernel_main_commit(&main_stack)) return 7;
    current_rsp = X86_KERNEL_MAIN_TOP - 256;
    entries = x86_stack_entries(3);
    for (slot = 0; slot <= 17; ++slot) {
        BOOT_U64 original = entries[slot];
        for (bit = 0; bit < 64; ++bit) {
            if (original && (bit == 5 || bit == 6)) continue;
            entries[slot] ^= 1ULL << bit;
            if (x86_kernel_main_retire(&main_stack) || x86_test_stack_invalidations() != 16) return 8;
            entries[slot] = original;
        }
    }
    return x86_kernel_main_retire(&main_stack) ? 0 : 9;
}

// ====== overlap fisico respinto prima di azzerare lo stack ancora attivo ===== //

static int overlap_tests(void)
{
    BOOT_U32 slot;
    volatile BOOT_U64 *entries;
    if (!fixture()) return 10;
    entries = x86_stack_entries(3);
    for (slot = 1; slot <= 16; ++slot) entries[slot] = (0x100000ULL + (slot - 1)*4096) | 3 | (1ULL << 63);
    if (x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info) ||
        x86_test_stack_invalidations() || x86_test_stack_pool()->FreePages != 32) return 11;
    return 0;
}

// ====== rollback del buffer assente senza modificare il vecchio mapping ===== //

static int failure_tests(void)
{
    if (!fixture()) return 12;
    x86_test_stack_missing(4);
    if (x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info) ||
        x86_test_stack_pool()->FreePages != 32 || x86_test_stack_invalidations() != 32 ||
        x86_test_stack_ordering() || main_stack.Phase) return 13;
    if (!fixture()) return 14;
    info.StackBytes = 4096;
    if (x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info) ||
        x86_test_stack_invalidations()) return 15;
    return 0;
}

// ====== tutte le foglie principali e le due guardie prima del commit ===== //

static int mapping_tests(void)
{
    BOOT_U32 slot, bit;
    volatile BOOT_U64 *entries;
    if (!fixture() || !x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info)) return 16;
    entries = x86_stack_entries(3);
    for (slot = 0; slot < 18; ++slot) {
        BOOT_U64 original = entries[96 + slot];
        for (bit = 0; bit < 64; ++bit) {
            if (original && (bit == 5 || bit == 6)) continue;
            entries[96 + slot] ^= 1ULL << bit;
            if (x86_kernel_main_release(&main_stack) || x86_kernel_main_commit(&main_stack) ||
                x86_test_stack_invalidations() != 16 || x86_test_stack_pool()->FreePages != 16) return 17;
            entries[96 + slot] = original;
        }
    }
    return x86_kernel_main_release(&main_stack) ? 0 : 18;
}

// ====== owner copiato e fasi alterate non possono revocare il mapping ===== //

static int metadata_tests(void)
{
    X86_KERNEL_MAIN_STACK copy;
    BOOT_U32 byte;
    if (!fixture()) return 19;
    if (x86_kernel_main_prepare(0, x86_test_stack_pool(), &info) ||
        x86_kernel_main_prepare(&main_stack, 0, &info) ||
        x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), 0) ||
        x86_kernel_main_prepare((X86_KERNEL_MAIN_STACK *)X86_KERNEL_MAIN_BASE, x86_test_stack_pool(), &info) ||
        x86_kernel_main_prepare((X86_KERNEL_MAIN_STACK *)X86_BOOT_STACK_GUARD, x86_test_stack_pool(), &info) ||
        x86_kernel_main_prepare((X86_KERNEL_MAIN_STACK *)X86_BOOT_STACK_TOP, x86_test_stack_pool(), &info) ||
        x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), (const BOOT_START_INFO *)&main_stack)) return 20;
    if (!x86_kernel_main_prepare(&main_stack, x86_test_stack_pool(), &info)) return 21;
    for (byte = 0; byte < sizeof(copy); ++byte) {
        ((unsigned char *)&copy)[byte] = ((const unsigned char *)&main_stack)[byte];
    }
    if (x86_kernel_main_release(&copy) || x86_kernel_main_commit(&copy)) return 22;
    main_stack.Reserved = 1;
    if (x86_kernel_main_release(&main_stack) || x86_kernel_main_commit(&main_stack)) return 23;
    main_stack.Reserved = 0;
    current_rsp = X86_KERNEL_MAIN_BASE;
    if (x86_kernel_main_release(&main_stack) || x86_kernel_main_commit(&main_stack)) return 24;
    current_rsp = X86_KERNEL_MAIN_BASE + 1;
    if (x86_kernel_main_release(&main_stack) || x86_kernel_main_commit(&main_stack)) return 35;
    current_rsp = X86_BOOT_STACK_TOP - 256;
    return x86_kernel_main_release(&main_stack) ? 0 : 25;
}

// ====== OOM e overlap con un lease vivo prima dell'allocazione principale ===== //

static int allocation_tests(void)
{
    KERNEL_PHYSICAL_LEASE lease = {0};
    BOOT_U32 page;
    KERNEL_PHYSICAL_ALLOCATOR *pool = x86_test_stack_pool();
    if (!fixture() || !kernel_physical_allocate(pool, 20, &lease)) return 26;
    if (x86_kernel_main_prepare(&main_stack, pool, &info) || pool->FreePages != 12 ||
        x86_test_stack_invalidations() || main_stack.Phase) return 27;
    if (!kernel_physical_release(pool, &lease) || !fixture() ||
        !kernel_physical_allocate(pool, 16, &lease)) return 28;
    for (page = 1; page <= 16; ++page) {
        x86_stack_entries(3)[page] = (lease.Base + (page - 1)*4096) | 3 | (1ULL << 63);
    }
    if (x86_kernel_main_prepare(&main_stack, pool, &info) || pool->FreePages != 16 ||
        x86_test_stack_invalidations()) return 29;
    return kernel_physical_release(pool, &lease) ? 0 : 30;
}

// ====== 128 riusi azzerati preservando il mapping loader e il budget ===== //

static int reuse_tests(void)
{
    BOOT_U32 cycle, byte;
    KERNEL_PHYSICAL_ALLOCATOR *pool = x86_test_stack_pool();
    if (!fixture()) return 31;
    for (cycle = 0; cycle < 128; ++cycle) {
        if (!x86_kernel_main_prepare(&main_stack, pool, &info)) return 32;
        for (byte = 0; byte < 65536; ++byte) {
            if (x86_stack_bytes(4)[byte]) return 33;
            x86_stack_bytes(4)[byte] = 0xa5;
        }
        if (!x86_kernel_main_release(&main_stack) || pool->FreePages != 32 ||
            x86_test_stack_invalidations() != (cycle + 1)*32 || x86_test_stack_ordering() ||
            !kernel_physical_validate(pool) || x86_stack_entries(3)[1] != (0x2000000ULL | 3 | (1ULL << 63))) return 34;
    }
    return 0;
}

// ====== verifica del nuovo ciclo di vita prima della transizione assembly ===== //

__declspec(dllexport) int run_main_stack_tests(void)
{
    int result;
    if ((result = lifecycle_tests()) != 0) return result;
    if ((result = retirement_tests()) != 0) return result;
    if ((result = overlap_tests()) != 0) return result;
    if ((result = failure_tests()) != 0) return result;
    if ((result = mapping_tests()) != 0) return result;
    if ((result = metadata_tests()) != 0) return result;
    if ((result = allocation_tests()) != 0) return result;
    return reuse_tests();
}
