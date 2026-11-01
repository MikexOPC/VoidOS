#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint32_t model_bsp = 1, model_supported = 1, model_image = 1, model_descriptors = 1;
uint32_t model_id;
uint32_t model_write_fault, model_self_fault, model_gs_reads, model_self_reads, model_writes;
uint64_t model_if = 0x200, model_gs, model_target;
jmp_buf model_terminal;
int model_expect_fatal;
const char *model_reason;

// ====== cpu_and_msr_model ======

uint32_t ks_boot_cpu(void) { return model_bsp; }
uint32_t xp_cpu_id(void) { return model_id; }
uint32_t xp_gs_supported(void) { return model_supported; }
uint64_t xp_gs_read(void) { ++model_gs_reads; return model_gs; }
uint64_t xp_gs_self(void) {
    ++model_self_reads;
    assert(model_gs && model_gs == model_target);
    return model_self_fault ? 0 : ((const X86_CPU_LOCAL *)(uintptr_t)model_gs)->Self;
}
void xp_gs_write(uint64_t base) {
    ++model_writes; model_target = base;
    if (!model_write_fault) model_gs = base;
}
int xp_image_contains(uintptr_t address, size_t bytes) {
    return model_image && ks_span((void *)address, bytes);
}
int xp_descriptors_ready(void) { return model_descriptors; }

// ====== irq_and_terminal_model ======

uint64_t ks_irq_save(void) { uint64_t saved = model_if; model_if = 0; return saved; }
uint64_t ks_irq_state(void) { return model_if; }
void ks_irq_restore(uint64_t flags) { assert(!(flags & ~0x200ULL)); model_if = flags; }
void ks_pause(void) { }
__declspec(noreturn) void ks_fail_stop(const char *reason) {
    model_reason = reason;
    if (model_expect_fatal) longjmp(model_terminal, 1);
    fprintf(stderr, "Unexpected fail-stop: %s\n", reason);
    exit(1);
}
