#define main xb_transition_cases_main
#include "../../transition/cases.c"
#undef main
#include "arch/x86/kernel/cpu/percpu/startup/installation/install.h"
#include "arch/x86/kernel/cpu/percpu/transition/content/ownership.h"
static XB_INSTALLATION image;
static unsigned char template_bytes[4096];

// ====== image_setup_private ======

static XS_ENTRY_RECORD image_setup_private(void) {
    setup_private();
    memset(&image, 0, sizeof(image));
    assert(x86_ap_low_reserve(&pool, &code) && xt_early(&pool, &code, &owner));
    XT_IO io = {transfer_private, safe_private, &pool};
    assert(xt_prepare(&owner, ROOT, 6, &io) == 1);
    XS_ENTRY_INPUT input = {1, 1, 40, 0, 520, code.Lease.Base, owner.Data.Base,
        owner.Tables.Base, ROOT, 0xffffffff80010000ULL, 0xffffffff80100000ULL, 6};
    XS_ENTRY_RECORD record;
    assert(xs_entry_build(&record, input));
    calls = fail_at = 0;
    return record;
}

// ====== image_release_private ======

static void image_release_private(void) {
    if (image.Self) assert(xb_erase(&image));
    assert(xt_verify(&owner) && xt_cancel(&owner) && x86_ap_low_release(&code));
    assert(pool.FreePages == 259 && kernel_physical_validate(&pool));
}

// ====== image_failure_private ======

static unsigned int image_failure_private(unsigned int failure) {
    XS_ENTRY_RECORD record = image_setup_private();
    BOOT_U64 budget = pool.FreePages;
    fail_at = failure;
    int result = xb_install(&image, &owner, template_bytes, record);
    unsigned int count = calls;
    fail_at = 0;
    if (failure) {
        assert(!result && (image.State == 0 || image.State == 3));
        if (image.Self) assert(!xb_installed(&image));
    } else {
        assert(result && image.State == 2 && xb_installed(&image));
        assert(xt_verify_tables(&owner) && !xt_verify(&owner) && !xt_freeze(&owner));
        assert(!xt_cancel(&owner));
        owner.State = 1;
        assert(!xt_build(&owner) && !xt_cancel(&owner));
        owner.State = 3;
        memset(pages[record.Data / 4096], 0, 4096);
        assert(!xt_content_unhold(&owner, &image));
        memcpy(pages[record.Data / 4096], image.Data, 4096);
        for (unsigned int page = 0; page < 2; ++page) {
            BOOT_U64 physical = page ? record.Data : record.Code;
            for (unsigned int byte = 0; byte < 4096; ++byte) {
                ((unsigned char *)pages[physical / 4096])[byte] ^= 1;
                assert(!xb_installed(&image));
                ((unsigned char *)pages[physical / 4096])[byte] ^= 1;
            }
        }
        XB_INSTALLATION copy = image;
        assert(!xb_installed(&copy) && !xb_erase(&copy));
        owner.Source[511] ^= 0x1000;
        assert(!xb_installed(&image) && !xb_erase(&image));
        owner.Source[511] ^= 0x1000;
        assert(xb_installed(&image));
    }
    assert(kernel_physical_budget_matches(&pool, budget));
    image_release_private();
    return count;
}

// ====== erase_failures_private ======

static unsigned int erase_failures_private(unsigned int failure) {
    XS_ENTRY_RECORD record = image_setup_private();
    assert(xb_install(&image, &owner, template_bytes, record));
    calls = 0; fail_at = failure;
    int result = xb_erase(&image);
    unsigned int count = calls;
    fail_at = 0;
    if (failure) {
        assert(!result && image.Self == (uint64_t)&image && image.State >= 2);
        assert(kernel_physical_lease_state(&pool, &owner.Data, 1));
        assert(kernel_physical_lease_state(&pool, &code.Lease, 1));
    } else assert(result && !image.Self);
    image_release_private();
    return count;
}

// ====== invalid_private ======

static void invalid_private(void) {
    XS_ENTRY_RECORD record = image_setup_private(), invalid = record;
    invalid.KernelRoot += 4096;
    assert(!xb_install(&image, &owner, template_bytes, invalid) && !calls && !image.Self);
    assert(!xb_install((XB_INSTALLATION *)&owner, &owner, template_bytes, record));
    assert(!xb_install(&image, &owner, (unsigned char *)&image, record));
    xt_is_bsp = 0;
    assert(!xb_install(&image, &owner, template_bytes, record));
    xt_is_bsp = 1;
    uint64_t saved_if = ks_irq_save();
    ks_irq_restore(1ULL << 9);
    assert(!xb_install(&image, &owner, template_bytes, record));
    (void)ks_irq_save();
    ks_irq_restore(saved_if);
    template_bytes[30] = 1;
    assert(!xb_install(&image, &owner, template_bytes, record) && !image.Self);
    template_bytes[30] = 0;
    image_release_private();
}

// ====== frozen_image_private ======

static void frozen_image_private(void) {
    XS_ENTRY_RECORD record = image_setup_private();
    assert(xb_install(&image, &owner, template_bytes, record));
    assert(x86_ap_low_freeze(&code, &owner));
    calls = 0;
    assert(!xb_erase(&image) && !xb_installed(&image) && !calls);
    assert(!xt_cancel(&owner) && !x86_ap_low_release(&code));
    assert(kernel_physical_lease_state(&pool, &owner.Data, 1));
    assert(kernel_physical_lease_state(&pool, &owner.Tables, 1));
    puts("PASS: frozen registry rejects installation mutation; six pinned pages retained, no AP");
}

// ====== main ======

#ifndef XB_INSTALLATION_ENTRY
#define XB_INSTALLATION_ENTRY main
#endif
int XB_INSTALLATION_ENTRY(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    FILE *file = NULL;
    int opened = fopen_s(&file, argv[1], "rb");
    assert(!opened && file);
    size_t read = fread(template_bytes, 1, 4096, file);
    int extra = fgetc(file), closed = fclose(file);
    assert(read == 4096 && extra == EOF && !closed);
    uint64_t saved_if = ks_irq_save();
    if (argc == 3) { assert(!strcmp(argv[2], "freeze")); frozen_image_private(); return 0; }
    invalid_private();
    unsigned int installs = image_failure_private(0), erases = erase_failures_private(0);
    for (unsigned int failure = 1; failure <= installs; ++failure) (void)image_failure_private(failure);
    for (unsigned int failure = 1; failure <= erases; ++failure) (void)erase_failures_private(failure);
    ks_irq_restore(saved_if);
    printf("PASS: installation %u I/O faults, erase %u I/O faults, 8192 physical byte corruptions, full budget restored\n", installs, erases);
    return 0;
}
