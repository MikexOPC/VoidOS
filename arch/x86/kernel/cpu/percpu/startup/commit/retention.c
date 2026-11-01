#include "internal/registry.h"
static const void *group_owner, *group_image, *group_transition, *group_resources;
static uint32_t group_count, group_phase;

// ====== xj_retained ======

int xj_retained(const void *resource) {
    return group_phase && resource && (resource == group_image || resource == group_transition || resource == group_resources);
}

// ====== xj_context_retained ======

int xj_context_retained(uint32_t index) { return group_phase && index && index < group_count; }

// ====== xj_phase ======

uint32_t xj_phase(const void *owner) { return owner && owner == group_owner ? group_phase : 0; }

// ====== xj_active ======

int xj_active(void) { return group_phase != 0; }

// ====== xj_latch ======

int xj_latch(const void *owner, const void *image, const void *transition, const void *resources, uint32_t count) {
    if (group_phase || !owner || !image || !transition || !resources || count < 2 || count > 64 ||
        owner == image || owner == transition || owner == resources || image == transition ||
        image == resources || transition == resources) return 0;
    group_owner = owner; group_image = image; group_transition = transition; group_resources = resources;
    group_count = count; group_phase = 1;
    return 1;
}

// ====== xj_finish ======

int xj_finish(const void *owner, uint32_t phase) {
    if (owner != group_owner || group_phase != 1 || (phase != 2 && phase != 3)) return 0;
    group_phase = phase;
    return 1;
}
