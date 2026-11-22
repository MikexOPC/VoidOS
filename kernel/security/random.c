#include "random.h"
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"
#include "kernel/sync/lock.h"

typedef struct {
    uint64_t     DrbgState[12];
    uint32_t     Source;
    uint32_t     Ready;
    KERNEL_LOCK  Lock;
} KERNEL_RANDOM_STATE;

static KERNEL_RANDOM_STATE random_state;
static uint64_t startup_samples[KERNEL_ENTROPY_STARTUP_COUNT];

// ====== zero_bytes ======

static void zero_bytes(void *ptr, uint32_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (uint32_t i = 0; i < len; ++i) {
        p[i] = 0U;
    }
}

// ====== encode_le64 ======

static void encode_le64(uint8_t *dst, uint64_t value)
{
    for (uint32_t i = 0; i < 8U; ++i) {
        dst[i] = (uint8_t)((value >> (i * 8U)) & 0xFFU);
    }
}

// ====== collect_hw_bytes ======

static int collect_hw_bytes(uint32_t source, uint8_t *out, uint32_t word_count)
{
    for (uint32_t i = 0; i < word_count; ++i) {
        uint64_t sample = 0;
        if (!kernel_arch_entropy_sample64(source, &sample)) {
            return 0;
        }
        encode_le64(out + i * 8U, sample);
    }
    return 1;
}

// ====== kernel_random_init ======

int kernel_random_init(void)
{
    if (random_state.Ready) {
        return 1;
    }
    uint32_t source = kernel_arch_entropy_detect();
    if (source == KERNEL_ENTROPY_SRC_NONE) {
        (void)kernel_log(3, "KERNEL_ENTROPY_ABSENT");
        return 0;
    }
    for (uint32_t i = 0; i < KERNEL_ENTROPY_STARTUP_COUNT; ++i) {
        if (!kernel_arch_entropy_sample64(source, &startup_samples[i])) {
            zero_bytes(startup_samples, (uint32_t)sizeof(startup_samples));
            (void)kernel_log(3, "KERNEL_ENTROPY_SAMPLE_FAILED");
            return 0;
        }
    }
    uint32_t healthy = ksec_health_check_u64(startup_samples, KERNEL_ENTROPY_STARTUP_COUNT);
    zero_bytes(startup_samples, (uint32_t)sizeof(startup_samples));
    if (!healthy) {
        (void)kernel_log(3, "KERNEL_ENTROPY_HEALTH_FAILED");
        return 0;
    }

    uint8_t entropy[32];
    uint8_t nonce[16];
    if (!collect_hw_bytes(source, entropy, 4U)) {
        zero_bytes(entropy, sizeof(entropy));
        (void)kernel_log(3, "KERNEL_ENTROPY_SEED_FAILED");
        return 0;
    }
    uint64_t n0 = kernel_arch_entropy_nonce64();
    uint64_t n1 = 0;
    if (!kernel_arch_entropy_sample64(source, &n1)) {
        n1 = kernel_arch_entropy_nonce64() ^ 0x9E3779B97F4A7C15ULL;
    }
    encode_le64(nonce, n0);
    encode_le64(nonce + 8U, n1);

    uint32_t ok = ksec_drbg_instantiate(
        random_state.DrbgState,
        entropy,
        (uint32_t)sizeof(entropy),
        nonce,
        (uint32_t)sizeof(nonce)
    );
    zero_bytes(entropy, sizeof(entropy));
    zero_bytes(nonce, sizeof(nonce));
    if (!ok) {
        ksec_drbg_wipe(random_state.DrbgState);
        (void)kernel_log(3, "KERNEL_ENTROPY_DRBG_FAILED");
        return 0;
    }

    uint64_t probe_a = 0;
    uint64_t probe_b = 0;
    if (!ksec_drbg_generate(random_state.DrbgState, (uint8_t *)&probe_a, 8U) ||
        !ksec_drbg_generate(random_state.DrbgState, (uint8_t *)&probe_b, 8U) ||
        probe_a == 0ULL || probe_b == 0ULL || probe_a == probe_b) {
        ksec_drbg_wipe(random_state.DrbgState);
        (void)kernel_log(3, "KERNEL_ENTROPY_SELFTEST_FAILED");
        return 0;
    }

    if (!random_state.Lock.Ready && !kernel_lock_initialize(&random_state.Lock, KERNEL_RANDOM_LOCK_RANK)) {
        ksec_drbg_wipe(random_state.DrbgState);
        return 0;
    }
    random_state.Source = source;
    random_state.Ready = 1U;
    return kernel_text("KERNEL_ENTROPY_SOURCE: ") && kernel_number(source) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_ENTROPY_READY");
}

// ====== kernel_random_ready ======

int kernel_random_ready(void)
{
    return random_state.Ready != 0U;
}

// ====== kernel_random_source ======

uint32_t kernel_random_source(void)
{
    return random_state.Source;
}

// ====== kernel_random_bytes ======

int kernel_random_bytes(void *out, uint32_t length)
{
    if (!random_state.Ready || (!out && length) || length > KERNEL_RANDOM_MAX_REQUEST) {
        return 0;
    }
    if (!length) {
        return 1;
    }
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *ctx = kernel_lock_current_context();
    if (!ctx) {
        ks_irq_restore(saved_if);
        return 0;
    }
    KERNEL_LOCK_GUARD guard = {0};
    if (!kernel_lock_acquire(&random_state.Lock, ctx, &guard)) {
        ks_irq_restore(saved_if);
        return 0;
    }
    int ok = ksec_drbg_generate(random_state.DrbgState, (uint8_t *)out, length) != 0U;
    if (!kernel_lock_release(ctx, &guard)) {
        ks_fail_stop("RANDOM_LOCK_RELEASE_FAILED");
    }
    ks_irq_restore(saved_if);
    return ok;
}

// ====== kernel_random_u64 ======

int kernel_random_u64(uint64_t *out)
{
    if (!out) {
        return 0;
    }
    return kernel_random_bytes(out, (uint32_t)sizeof(*out));
}

// ====== kernel_random_reseed ======

int kernel_random_reseed(void)
{
    if (!random_state.Ready) {
        return 0;
    }
    uint8_t entropy[32];
    if (!collect_hw_bytes(random_state.Source, entropy, 4U)) {
        zero_bytes(entropy, sizeof(entropy));
        return 0;
    }
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *ctx = kernel_lock_current_context();
    if (!ctx) {
        ks_irq_restore(saved_if);
        zero_bytes(entropy, sizeof(entropy));
        return 0;
    }
    KERNEL_LOCK_GUARD guard = {0};
    if (!kernel_lock_acquire(&random_state.Lock, ctx, &guard)) {
        ks_irq_restore(saved_if);
        zero_bytes(entropy, sizeof(entropy));
        return 0;
    }
    int ok = ksec_drbg_reseed(random_state.DrbgState, entropy, (uint32_t)sizeof(entropy)) != 0U;
    if (!kernel_lock_release(ctx, &guard)) {
        ks_fail_stop("RANDOM_LOCK_RELEASE_FAILED");
    }
    ks_irq_restore(saved_if);
    zero_bytes(entropy, sizeof(entropy));
    return ok;
}
