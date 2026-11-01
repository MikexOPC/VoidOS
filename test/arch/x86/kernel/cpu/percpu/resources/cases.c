#include "model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ====== bind_private ======

static int bind_private(X86_AP_RESOURCES *owner) {
    return x86_ap_bind(&context_model, &window_model, NULL, &inventory_model, capture_model, cpus_model, owner);
}

// ====== main ======

int main(void) {
    for (unsigned int cpus = 1; cpus <= 64; ++cpus) {
        assert(x86_ap_tables(cpus) == xa_geometry_tables(cpus));
        for (unsigned int page = 0; page < 4096; ++page) assert(x86_ap_page(cpus, page) == xa_geometry_page(cpus, page));
    }
    assert(xa_geometry_page(65, UINT64_MAX) == UINT32_MAX);
    for (unsigned int cpu = 1; cpu < 64; ++cpu) {
        X86_KERNEL_SEGMENTS descriptors;
        assert(x86_ap_descriptor_prepare(&descriptors, cpu) && x86_ap_descriptor_verify(&descriptors, cpu));
        for (unsigned int bit = 0; bit < sizeof(descriptors) * 8; ++bit) {
            ((unsigned char *)&descriptors)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
            assert(!x86_ap_descriptor_verify(&descriptors, cpu));
            ((unsigned char *)&descriptors)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
        }
    }
    cpus_model = 4; model_reset(); X86_AP_RESOURCES owner = {0};
    assert(!bind_private((X86_AP_RESOURCES *)&pool_model));
    assert(!bind_private((X86_AP_RESOURCES *)&window_model));
    assert(!bind_private((X86_AP_RESOURCES *)&context_model));
    assert(!bind_private((X86_AP_RESOURCES *)&inventory_model));
    assert(!bind_private((X86_AP_RESOURCES *)(uintptr_t)(UINTPTR_MAX - 7)));
    calls = 0;
    assert(bind_private(&owner)); unsigned int steps = calls;
    assert(live == 2 && pins == 2 && !bind_private(&owner));
    bad_flush = 1; assert(!x86_ap_unbind(&owner));
    assert(live == 2 && pins == 2 && owner.Published && owner.Registered);
    assert(x86_ap_unbind(&owner) && !live && !pins && pool_model.FreePages == 4096);
    model_reset(); assert(bind_private(&owner)); bad_release = 1;
    assert(!x86_ap_unbind(&owner) && live == 2 && pins == 2 && !owner.Published && !owner.Registered);
    assert(owner.TablesPinned && owner.DataPinned && x86_ap_unbind(&owner));
    model_reset(); assert(bind_private(&owner)); bad_release = 2;
    assert(!x86_ap_unbind(&owner) && live == 1 && pins == 1 && !owner.TablesAllocated && owner.DataPinned);
    assert(x86_ap_unbind(&owner) && !live && !pins && pool_model.FreePages == 4096);
    for (unsigned int fail = 1; fail <= steps; ++fail) {
        model_reset(); memset(&owner, 0, sizeof(owner)); fail_at = fail;
        assert(!bind_private(&owner));
        assert(!live && !pins && !root_model[X86_AP_SLOT] && !x86_ap_owner() && pool_model.FreePages == 4096);
        for (unsigned int byte = 0; byte < sizeof(owner); ++byte) assert(!((unsigned char *)&owner)[byte]);
    }
    model_reset(); alias_model = 1; assert(!bind_private(&owner)); assert(!live && !pins);
    cpus_model = 64; model_reset(); assert(bind_private(&owner));
    assert(owner.Authority.Data.Pages == 1827 && owner.Authority.Tables.Pages == 10);
    assert(x86_ap_unbind(&owner) && !live && !pins && pool_model.FreePages == 4096);
    model_reset(); assert(bind_private(&owner));
    fail_at = calls + 1;
    assert(!x86_ap_commit(&owner) && !owner.Committed && live == 2 && pins == 2);
    assert(x86_ap_unbind(&owner) && !live && !pins);
    model_reset(); assert(bind_private(&owner));
    assert(x86_ap_commit(&owner) && owner.Committed && !x86_ap_commit(&owner));
    X86_AP_RESOURCES committed = owner;
    BOOT_U64 root_before = root_model[X86_AP_SLOT], budget_before = pool_model.FreePages;
    unsigned int calls_before = calls;
    assert(!x86_ap_unbind(&owner) && !memcmp(&owner, &committed, sizeof(owner)));
    assert(root_model[X86_AP_SLOT] == root_before && pool_model.FreePages == budget_before &&
        calls == calls_before && live == 2 && pins == 2 && x86_ap_owner() == &owner.Authority);
    printf("AP_RESOURCES_PASS: 64 geometries, 63 descriptor bit-flip sweeps, %u injected forward failures, alias refusal, retained pins on failed flush, 64-CPU model budget restored\n", steps);
    return 0;
}
