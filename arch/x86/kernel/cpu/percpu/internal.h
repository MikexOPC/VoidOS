#ifndef OS_X86_PERCPU_INTERNAL_H
#define OS_X86_PERCPU_INTERNAL_H
#include "percpu.h"
#include "kernel/sync/internal.h"

// ====== privileged_backend_contract ======

uint32_t ks_boot_cpu(void);
uint32_t xp_gs_supported(void);
uint32_t xp_cpu_id(void);
uint64_t xp_gs_read(void);
uint64_t xp_gs_self(void);
void xp_gs_write(uint64_t base);
int xp_image_contains(uintptr_t address, size_t bytes);
int xp_descriptors_ready(void);
const X86_CPU_LOCAL *x86_percpu_ap(uint32_t index);
int x86_percpu_ap_prepare(uint32_t index, const KERNEL_LOCK_WAIT *wait);
int x86_percpu_ap_cancel(uint32_t index);
int x86_percpu_ap_seal(uint32_t count);
int x86_percpu_ap_sealed(uint32_t count);
uint32_t x86_percpu_ap_closed(void);
int x86_percpu_ap_access_close(uint32_t count, const uint32_t *ids);
int x86_percpu_ap_enter(uint32_t index, uint32_t id, uint64_t local, const KERNEL_LOCK_WAIT *wait);
int x86_percpu_ap_accept(uint32_t index);
int x86_percpu_ap_reject(uint32_t index);
uint64_t x86_percpu_ap_phase(uint32_t index);
#endif
