#ifndef OS_KERNEL_SECURITY_RANDOM_H
#define OS_KERNEL_SECURITY_RANDOM_H

#include <stdint.h>

// ====== entropy_and_csprng_contract ======

// Lock order: security random (120) < process table (150) < heap (200) < physical (300).
#define KERNEL_RANDOM_LOCK_RANK       120U
#define KERNEL_ENTROPY_SRC_NONE       0U
#define KERNEL_ENTROPY_SRC_RDRAND     1U
#define KERNEL_ENTROPY_SRC_RDSEED     2U
#define KERNEL_ENTROPY_STARTUP_COUNT  1024U
#define KERNEL_RANDOM_MAX_REQUEST     4096U

// Architecture backend hooks (implemented in arch/<arch>/kernel/security/entropy.c).
uint32_t kernel_arch_entropy_detect(void);
int      kernel_arch_entropy_sample64(uint32_t source, uint64_t *out);
uint64_t kernel_arch_entropy_nonce64(void);

// Rust FFI primitives (implemented in kernel/security/rust/csprng.rs).
uint32_t ksec_health_check_u64(const uint64_t *samples, uint32_t count);
uint32_t ksec_drbg_instantiate(
    uint64_t *state_words,
    const uint8_t *entropy,
    uint32_t entropy_len,
    const uint8_t *nonce,
    uint32_t nonce_len
);
uint32_t ksec_drbg_generate(uint64_t *state_words, uint8_t *out, uint32_t out_len);
uint32_t ksec_drbg_reseed(uint64_t *state_words, const uint8_t *entropy, uint32_t entropy_len);
void     ksec_drbg_wipe(uint64_t *state_words);

// Public kernel CSPRNG API.
int      kernel_random_init(void);
int      kernel_random_ready(void);
uint32_t kernel_random_source(void);
int      kernel_random_bytes(void *out, uint32_t length);
int      kernel_random_u64(uint64_t *out);
int      kernel_random_reseed(void);

#endif
