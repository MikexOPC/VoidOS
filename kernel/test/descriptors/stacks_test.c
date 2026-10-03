#include "arch/x86/kernel/descriptors/stacks.h"
static X86_KERNEL_STACKS stacks;
static KERNEL_PHYSICAL_ALLOCATOR physical;
static BOOT_U64 tables[4][512];
static unsigned char data[3][16384], map[40];
static BOOT_U32 invalidations, ordering, missing;

// ====== viste host indipendenti dalla finestra di allocazione ===== //

volatile BOOT_U64 *x86_stack_entries(BOOT_U32 level)
{
    return level < 4 ? tables[level] : 0;
}

// ====== guasto controllato del prestito senza puntatori non validi ===== //

volatile unsigned char *x86_stack_bytes(BOOT_U32 stack)
{
    return stack >= 1 && stack <= 3 && stack != missing ? data[stack - 1] : 0;
}

// ====== intero batch pubblicato prima della prima invalidazione ===== //

void x86_stack_invalidate(BOOT_U64 address)
{
    BOOT_U32 slot;
    BOOT_U64 offset = address - X86_KERNEL_IST_GUARD;
    if (address < X86_KERNEL_IST_GUARD || offset >= 65536 || (offset & 4095) ||
        !x86_ist_page((BOOT_U32)(offset / 4096)) ||
        !kernel_physical_is_pinned(&physical, &stacks.Lease)) ordering = 1;
    for (slot = 0; slot < 16; ++slot) {
        if (x86_ist_page(slot) && ((!tables[3][64 + slot]) != (stacks.Ready != 0))) ordering = 1;
    }
    ++invalidations;
}

// ====== reset esplicito per evitare chiamate implicite al CRT ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 byte;
    for (byte = 0; byte < bytes; ++byte) ((volatile unsigned char *)object)[byte] = 0;
}

// ====== trentadue pagine disponibili e parent supervisor RW ===== //

static int fixture(void)
{
    BOOT_U32 byte, stack;
    clear(&stacks, sizeof(stacks)); clear(&physical, sizeof(physical));
    clear(tables, sizeof(tables)); clear(map, sizeof(map));
    map[0] = 7; map[9] = 0x10; map[24] = 32; map[32] = 8;
    tables[0][X86_BOOT_RECURSIVE_SLOT] = 0x1003 | (1ULL << 63);
    tables[0][(X86_KERNEL_IST_GUARD >> 39) & 511] = 0x2003;
    tables[1][0] = 0x3003; tables[2][0] = 0x4003;
    invalidations = ordering = missing = 0;
    for (stack = 0; stack < 3; ++stack) {
        for (byte = 0; byte < 16384; ++byte) ((volatile unsigned char *)data[stack])[byte] = 0xa5;
    }
    return kernel_physical_initialize(&physical, map, 40, 40, 36);
}

// ====== ownership guardie zeroing commit e rilascio vietato dopo pubblicazione ===== //

static int ownership_tests(void)
{
    BOOT_U32 slot, stack, byte;
    if (!fixture() || !x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) ||
        !x86_kernel_stacks_validate(&stacks) || invalidations != 12 || ordering || physical.FreePages != 20) return 1;
    for (stack = 0; stack < 3; ++stack) for (byte = 0; byte < 16384; ++byte) if (data[stack][byte]) return 2;
    for (slot = 0; slot < 16; ++slot) {
        BOOT_U32 page = x86_ist_page(slot);
        BOOT_U64 expected = page ? (stacks.Lease.Base + (page - 1) * 4096) | 3 | (1ULL << 63) : 0;
        if (tables[3][64 + slot] != expected) return 3;
    }
    if (kernel_physical_release(&physical, &stacks.Lease) ||
        !x86_kernel_stacks_release(&stacks) || invalidations != 24 || ordering || physical.FreePages != 32) return 4;
    if (stacks.Pool || stacks.Lease.Id || stacks.Ready || x86_kernel_stacks_release(&stacks)) return 5;
    if (!x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) || !x86_kernel_stacks_commit(&stacks) ||
        x86_kernel_stacks_release(&stacks) || x86_kernel_stacks_commit(&stacks) ||
        !x86_kernel_stacks_validate(&stacks)) return 6;
    if (!fixture()) return 7;
    missing = 2;
    if (x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) || physical.FreePages != 32 ||
        stacks.Ready || stacks.Lease.Id || invalidations != 24 || ordering) return 8;
    for (slot = 64; slot < 80; ++slot) if (tables[3][slot]) return 9;
    if (!fixture()) return 10;
    tables[3][69] = 0x9003;
    if (x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) || physical.NextId != 1 || invalidations) return 11;
    if (!fixture() || !x86_kernel_stacks_acquire(&stacks, &physical, 0x1000)) return 12;
    tables[3][78] ^= 4;
    if (x86_kernel_stacks_validate(&stacks) || x86_kernel_stacks_release(&stacks)) return 13;
    tables[3][78] ^= 4;
    if (!x86_kernel_stacks_release(&stacks)) return 14;
    if (!fixture()) return 15;
    map[24] = 8;
    clear(&physical, sizeof(physical));
    if (!kernel_physical_initialize(&physical, map, 40, 40, 36) ||
        x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) || physical.FreePages != 8 || invalidations) return 16;
    return 0;
}

// ====== ogni bit significativo delle sedici PTE e delle quattro guardie ===== //

static int corruption_tests(void)
{
    BOOT_U32 slot, bit;
    if (!fixture() || !x86_kernel_stacks_acquire(&stacks, &physical, 0x1000)) {
        return 17;
    }
    for (slot = 0; slot < 16; ++slot) {
        BOOT_U64 original = tables[3][64 + slot];
        for (bit = 0; bit < 64; ++bit) {
            int accessed_dirty = original && (bit == 5 || bit == 6);
            tables[3][64 + slot] = original ^ (1ULL << bit);
            if (accessed_dirty) {
                if (!x86_kernel_stacks_validate(&stacks)) return 18;
            } else {
                if (x86_kernel_stacks_validate(&stacks) ||
                    x86_kernel_stacks_release(&stacks) ||
                    physical.FreePages != 20 || invalidations != 12 ||
                    !kernel_physical_is_pinned(&physical, &stacks.Lease)) return 19;
            }
            tables[3][64 + slot] = original;
        }
    }
    return x86_kernel_stacks_release(&stacks) && !ordering ? 0 : 20;
}

// ====== mapping preesistenti mai cancellati oppure sostituiti ===== //

static int foreign_mapping_tests(void)
{
    BOOT_U32 slot, level;
    for (slot = 0; slot < 16; ++slot) {
        if (!fixture()) return 21;
        tables[3][64 + slot] = 0x9003;
        if (x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) ||
            physical.NextId != 1 || physical.FreePages != 32 ||
            tables[3][64 + slot] != 0x9003 || invalidations) return 22;
    }
    for (level = 0; level < 3; ++level) {
        BOOT_U32 index = level ? 0 : (BOOT_U32)((X86_KERNEL_IST_GUARD >> 39) & 511);
        const BOOT_U64 invalid_flags[] = {1, 2, 4, 8, 16, 128, 256, 512, 1ULL << 63};
        BOOT_U32 flag;
        for (flag = 0; flag < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++flag) {
            if (!fixture()) return 23;
            tables[level][index] ^= invalid_flags[flag];
            if (x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) ||
                physical.FreePages != 32 || invalidations) return 24;
        }
    }
    return 0;
}

// ====== owner spostato alias input nulli e intervalli virtuali riservati ===== //

static int metadata_tests(void)
{
    X86_KERNEL_STACKS copy;
    BOOT_U32 byte;
    if (!fixture()) return 25;
    if (x86_kernel_stacks_acquire(0, &physical, 0x1000) ||
        x86_kernel_stacks_acquire(&stacks, 0, 0x1000) ||
        x86_kernel_stacks_acquire(&stacks, &physical, 0x2000) ||
        x86_kernel_stacks_acquire((X86_KERNEL_STACKS *)&physical, &physical, 0x1000) ||
        x86_kernel_stacks_acquire((X86_KERNEL_STACKS *)X86_KERNEL_IST_GUARD, &physical, 0x1000) ||
        x86_kernel_stacks_acquire(&stacks, (KERNEL_PHYSICAL_ALLOCATOR *)X86_KERNEL_IST_GUARD, 0x1000) ||
        physical.FreePages != 32 || invalidations) return 26;
    if (!x86_kernel_stacks_acquire(&stacks, &physical, 0x1000)) return 27;
    for (byte = 0; byte < sizeof(copy); ++byte) {
        ((unsigned char *)&copy)[byte] = ((const unsigned char *)&stacks)[byte];
    }
    if (x86_kernel_stacks_validate(&copy) || x86_kernel_stacks_release(&copy) ||
        x86_kernel_stacks_commit(&copy) || physical.FreePages != 20) return 28;
    stacks.Committed = 2;
    if (x86_kernel_stacks_validate(&stacks) || x86_kernel_stacks_release(&stacks)) return 29;
    stacks.Committed = 0;
    return x86_kernel_stacks_release(&stacks) ? 0 : 30;
}

// ====== rollback di ogni stack mancante senza perdita di pagine ===== //

static int rollback_tests(void)
{
    BOOT_U32 stack, slot;
    for (stack = 1; stack <= 3; ++stack) {
        if (!fixture()) return 31;
        missing = stack;
        if (x86_kernel_stacks_acquire(&stacks, &physical, 0x1000) ||
            physical.FreePages != 32 || invalidations != 24 || ordering ||
            stacks.Self || stacks.Pool || stacks.Lease.Id) return 32;
        for (slot = 64; slot < 80; ++slot) {
            if (tables[3][slot]) return 33;
        }
    }
    return 0;
}

// ====== riuso zeroed con budget e invalidazioni esatti per 256 cicli ===== //

static int reuse_tests(void)
{
    BOOT_U32 cycle, stack, byte;
    if (!fixture()) return 34;
    for (cycle = 0; cycle < 256; ++cycle) {
        if (!x86_kernel_stacks_acquire(&stacks, &physical, 0x1000)) return 35;
        for (stack = 0; stack < 3; ++stack) {
            for (byte = 0; byte < 16384; ++byte) {
                if (data[stack][byte]) return 36;
                ((volatile unsigned char *)data[stack])[byte] = 0xa5;
            }
        }
        if (!x86_kernel_stacks_release(&stacks) || physical.FreePages != 32 ||
            invalidations != (cycle + 1) * 24 || ordering ||
            !kernel_physical_validate(&physical)) return 37;
    }
    return 0;
}

// ====== suite bounded con codici distinti per ogni gruppo di errori ===== //

__declspec(dllexport) int run_stack_tests(void)
{
    int result;
    if ((result = ownership_tests()) != 0) return result;
    if ((result = corruption_tests()) != 0) return result;
    if ((result = foreign_mapping_tests()) != 0) return result;
    if ((result = metadata_tests()) != 0) return result;
    if ((result = rollback_tests()) != 0) return result;
    return reuse_tests();
}
