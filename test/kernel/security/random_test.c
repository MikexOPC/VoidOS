#include "kernel/security/random.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uintptr_t ksec_test_cookie;
extern uintptr_t ksec_test_cookie_comp;
extern void kernel_security_cookie_validate(uintptr_t cookie);

static jmp_buf panic_jmp;
static const char *last_panic = NULL;

__declspec(noreturn) void kernel_panic(const char *reason)
{
    last_panic = reason;
    longjmp(panic_jmp, 1);
}

// Host test of kernel_random_* over a controllable simulated hardware entropy backend.

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL security_random_host line %d\n", __LINE__); \
        exit(1); \
    } \
} while (0)

static uint32_t sim_source = KERNEL_ENTROPY_SRC_NONE;
static int sim_stuck = 0;
static uint64_t sim_counter = 1ULL;

int kernel_text(const char *t) { (void)t; return 1; }
int kernel_number(unsigned long long n) { (void)n; return 1; }
int kernel_log(int level, const char *t) { (void)level; (void)t; return 1; }

// ====== kernel_arch_entropy_detect ======

uint32_t kernel_arch_entropy_detect(void)
{
    return sim_source;
}

// ====== kernel_arch_entropy_sample64 ======

int kernel_arch_entropy_sample64(uint32_t source, uint64_t *out)
{
    if (!out || source == KERNEL_ENTROPY_SRC_NONE) {
        return 0;
    }
    if (sim_stuck) {
        *out = 0x5555555555555555ULL;
        return 1;
    }
    sim_counter = sim_counter * 6364136223846793005ULL + 1442695040888963407ULL;
    *out = sim_counter;
    return 1;
}

// ====== kernel_arch_entropy_nonce64 ======

uint64_t kernel_arch_entropy_nonce64(void)
{
    return 0x123456789ABCDEF0ULL ^ sim_counter;
}

// ====== main ======

int main(void)
{
    // 1) Absent hardware source must fail cleanly.
    sim_source = KERNEL_ENTROPY_SRC_NONE;
    CHECK(!kernel_random_init());
    CHECK(!kernel_random_ready());

    // 2) Stuck hardware RNG must be rejected by the NIST SP 800-90B RCT health test.
    sim_source = KERNEL_ENTROPY_SRC_RDRAND;
    sim_stuck = 1;
    CHECK(!kernel_random_init());
    CHECK(!kernel_random_ready());

    // 3) Healthy RDSEED source initializes HMAC_DRBG and generates distinct values.
    sim_source = KERNEL_ENTROPY_SRC_RDSEED;
    sim_stuck = 0;
    CHECK(kernel_random_init());
    CHECK(kernel_random_ready());
    CHECK(kernel_random_source() == KERNEL_ENTROPY_SRC_RDSEED);

    uint64_t a = 0, b = 0;
    CHECK(kernel_random_u64(&a));
    CHECK(kernel_random_u64(&b));
    CHECK(a != 0ULL && b != 0ULL && a != b);

    uint8_t buf1[64] = {0};
    uint8_t buf2[64] = {0};
    CHECK(kernel_random_bytes(buf1, sizeof(buf1)));
    CHECK(kernel_random_bytes(buf2, sizeof(buf2)));
    CHECK(memcmp(buf1, buf2, sizeof(buf1)) != 0);

    // 4) Reseed succeeds and rejects over-limit requests.
    CHECK(kernel_random_reseed());
    CHECK(!kernel_random_bytes(buf1, KERNEL_RANDOM_MAX_REQUEST + 1U));
    CHECK(!kernel_random_bytes(NULL, 16U));
    CHECK(kernel_random_bytes(NULL, 0U));

    // 5) Stack canary cookie initialization and corruption detection.
    uintptr_t initial_cookie = ksec_test_cookie;
    CHECK(kernel_security_cookie_init());
    CHECK(ksec_test_cookie != 0U && ksec_test_cookie != initial_cookie);
    CHECK((ksec_test_cookie >> 48) == 0U);
    CHECK(ksec_test_cookie_comp == ~ksec_test_cookie);
    kernel_security_cookie_validate(ksec_test_cookie);

    last_panic = NULL;
    if (setjmp(panic_jmp) == 0) {
        kernel_security_cookie_validate(ksec_test_cookie ^ 1ULL);
        CHECK(0); // Must never return on corrupted cookie.
    }
    CHECK(last_panic != NULL && strcmp(last_panic, "STACK_COOKIE_CORRUPTED") == 0);

    printf("PASS: security_random_host (entropy, RCT+APT, HMAC_DRBG, /GS stack canary + corruption panic)\n");
    return 0;
}
