#include "kernel/security/random.h"
#include <immintrin.h>
#include <intrin.h>

// Intel SDM Vol. 2A, Table 3-10: CPUID.(EAX=01H):ECX.RDRAND[bit 30].
#define X86_CPUID_ECX_RDRAND  (1U << 30)
// Intel SDM Vol. 2A, Table 3-8: CPUID.(EAX=07H, ECX=0H):EBX.RDSEED[bit 18].
#define X86_CPUID_EBX_RDSEED  (1U << 18)

// Intel DRNG Software Implementation Guide Section 5.2.1: retry RDRAND up to 10 times;
// RDSEED draws directly from the entropy conditioner so allow up to 64 retries.
#define X86_RDRAND_MAX_RETRIES  10U
#define X86_RDSEED_MAX_RETRIES  64U

// ====== kernel_arch_entropy_detect ======

uint32_t kernel_arch_entropy_detect(void)
{
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 0);
    uint32_t max_leaf = (uint32_t)regs[0];
    if (max_leaf >= 7U) {
        __cpuidex(regs, 7, 0);
        if (((uint32_t)regs[1] & X86_CPUID_EBX_RDSEED) != 0U) {
            return KERNEL_ENTROPY_SRC_RDSEED;
        }
    }
    if (max_leaf >= 1U) {
        __cpuid(regs, 1);
        if (((uint32_t)regs[2] & X86_CPUID_ECX_RDRAND) != 0U) {
            return KERNEL_ENTROPY_SRC_RDRAND;
        }
    }
    return KERNEL_ENTROPY_SRC_NONE;
}

// ====== kernel_arch_entropy_sample64 ======

int kernel_arch_entropy_sample64(uint32_t source, uint64_t *out)
{
    if (!out) {
        return 0;
    }
    unsigned long long value = 0ULL;
    if (source == KERNEL_ENTROPY_SRC_RDSEED) {
        // Intel SDM Vol. 2B, RDSEED: CF=1 indicates a valid seed value.
        for (uint32_t attempt = 0U; attempt < X86_RDSEED_MAX_RETRIES; ++attempt) {
            if (_rdseed64_step(&value)) {
                *out = (uint64_t)value;
                return 1;
            }
            _mm_pause();
        }
        return 0;
    }
    if (source == KERNEL_ENTROPY_SRC_RDRAND) {
        // Intel SDM Vol. 2B, RDRAND: CF=1 indicates a valid random value.
        for (uint32_t attempt = 0U; attempt < X86_RDRAND_MAX_RETRIES; ++attempt) {
            if (_rdrand64_step(&value)) {
                *out = (uint64_t)value;
                return 1;
            }
            _mm_pause();
        }
        return 0;
    }
    return 0;
}

// ====== kernel_arch_entropy_nonce64 ======

uint64_t kernel_arch_entropy_nonce64(void)
{
    // Intel SDM Vol. 2B, RDTSC: used only as personalization/nonce, never credited as entropy.
    return (uint64_t)__rdtsc();
}
