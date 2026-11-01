#define XB_INSTALLATION_ENTRY xb_installation_cases_main
#include "../installation/cases.c"
#include "arch/x86/kernel/cpu/percpu/startup/commit/commit.h"
#include <stdlib.h>
static XJ_GROUP group;
static struct { uint32_t Check, Seal, Verify, Mode, Contexts; } private_model;

// ====== joint_check_private ======

static int joint_check_private(void *context, XB_INSTALLATION *installed, uint32_t cpus) {
    assert(context == &private_model && installed == &image && cpus == 4);
    assert(!xj_retained(&owner) && !xj_context_retained(1));
    ++private_model.Check;
    return private_model.Mode != 1;
}

// ====== joint_seal_private ======

static int joint_seal_private(void *context, uint32_t cpus) {
    assert(context == &private_model && cpus == 4);
    assert(xj_retained(context) && xj_retained(&image) && xj_retained(&owner));
    assert(xj_context_retained(1) && xj_context_retained(3) && !xj_context_retained(4));
    assert(!xb_erase(&image) && !xt_cancel(&owner) && !xt_content_unhold(&owner, &image));
    if (private_model.Mode == 2) return 0;
    ++private_model.Seal;
    if (private_model.Mode == 3) return 0;
    ++private_model.Contexts;
    return 1;
}

// ====== joint_verify_private ======

static int joint_verify_private(void *context, uint32_t cpus) {
    assert(context == &private_model && cpus == 4 && private_model.Seal == 1 && private_model.Contexts == 1);
    ++private_model.Verify;
    return private_model.Mode != 4;
}

// ====== rejected_cleanup_private ======

static void rejected_cleanup_private(void) {
    unsigned int previous_calls = calls;
    assert(!xb_erase(&image) && !xt_cancel(&owner) && !x86_ap_low_release(&code));
    assert(!xt_content_unhold(&owner, &image));
    assert(calls == previous_calls && kernel_physical_lease_state(&pool, &owner.Data, 1));
    assert(kernel_physical_lease_state(&pool, &owner.Tables, 1));
    assert(kernel_physical_lease_state(&pool, &code.Lease, 1));
    assert(kernel_physical_budget_matches(&pool, 253) && kernel_physical_validate(&pool));
    assert(xj_retained(&private_model) && xj_retained(&image) && xj_retained(&owner));
    XJ_GROUP copied = group;
    assert(!xj_phase(&copied));
    group.State = group.Failure = 0;
    image.State = 2; owner.State = 3;
    assert(!xb_erase(&image) && !xt_cancel(&owner) && !xt_build(&owner));
    assert(xj_phase(&group) && xj_context_retained(1));
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 3 || argc == 4);
    FILE *file = NULL;
    int opened = fopen_s(&file, argv[1], "rb");
    assert(!opened && file);
    size_t bytes = fread(template_bytes, 1, 4096, file);
    int extra = fgetc(file), closed = fclose(file);
    assert(bytes == 4096 && extra == EOF && !closed);
    (void)ks_irq_save();
    XS_ENTRY_RECORD record = image_setup_private();
    assert(xb_install(&image, &owner, template_bytes, record));
    XJ_OPERATIONS ops = {&private_model, &private_model, 4, sizeof(private_model),
        joint_check_private, joint_seal_private, joint_verify_private};
    XJ_OPERATIONS bad = ops;
    bad.Resources = &owner;
    assert(!xj_commit(&group, &image, &bad) && !xj_phase(&group));
    bad = ops; bad.Bytes = 0;
    assert(!xj_commit(&group, &image, &bad));
    bad = ops; bad.Cpus = 1;
    assert(!xj_commit(&group, &image, &bad));
    assert(!xj_commit((XJ_GROUP *)&image, &image, &ops));
    assert(!xj_commit((XJ_GROUP *)template_bytes, &image, &ops));
    private_model.Mode = !strcmp(argv[2], "preflight") ? 1U : !strcmp(argv[2], "seal-before") ? 2U :
        !strcmp(argv[2], "seal-after") ? 3U : !strcmp(argv[2], "verify") ? 4U : 0U;
    if (argc == 4) {
        char *end = NULL;
        unsigned long number = strtoul(argv[3], &end, 10);
        assert(!strcmp(argv[2], "io") && end && !*end && number && number <= 48);
        fail_at = (unsigned int)number;
    }
    else assert(private_model.Mode || !strcmp(argv[2], "positive"));
    calls = 0; xt_stop_armed = 1;
    if (!setjmp(xt_terminal)) {
        int committed = xj_commit(&group, &image, &ops);
        unsigned int total = calls;
        if (!committed) {
            assert(!xj_phase(&group) && !group.Self && !xj_retained(&owner) && !private_model.Seal);
            fail_at = 0;
            image_release_private();
            puts("PASS: joint preflight rejection; complete rollback and original budget");
        } else {
            assert(!private_model.Mode && !fail_at && total == 48);
            assert(xj_phase(&group) == 2 && group.State == 2 && !group.Failure && xb_sealed(&image));
            pages[record.Code / 4096][0] ^= 1;
            assert(!xb_sealed(&image)); pages[record.Code / 4096][0] ^= 1;
            assert(xb_sealed(&image));
            assert(!xj_commit(&group, &image, &ops));
            rejected_cleanup_private();
            puts("PASS: joint seal, 48 I/O points, copied owner/corruption/retry/state tampering; six pinned pages retained");
        }
    } else {
        assert(xj_phase(&group) == 3 && group.State == 3 && group.Failure >= 1 && group.Failure <= 3);
        assert(strstr(xt_reason, "AP_JOINT_") == xt_reason);
        if (private_model.Mode == 2) assert(!private_model.Seal && !private_model.Contexts);
        if (private_model.Mode == 3) assert(private_model.Seal == 1 && !private_model.Contexts);
        fail_at = 0;
        assert(!xj_commit(&group, &image, &ops));
        rejected_cleanup_private();
        puts("PASS: joint failure is terminal and retained; no successful seal publication or release");
    }
    return 0;
}
