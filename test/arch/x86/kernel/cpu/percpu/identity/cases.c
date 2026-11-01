#include "../platform.h"
#include "arch/x86/kernel/cpu/percpu/identity/identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t trusted = 1;

// ====== x86_cpu_identity_inputs_model ======

int x86_cpu_identity_inputs(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits) {
    return trusted && bits == 48 && topology->Ready == 1 && profile->Ready == 1;
}

// ====== plan_private ======

static void plan_private(void) {
    _Alignas(8) KERNEL_MADT_CPU records[65] = {{0}};
    X86_CPU_IDENTITIES output, before;
    memset(&output, 0xa5, sizeof(output)); before = output;
    assert(!x86_cpu_identity_plan(NULL, 1, 7, 90, &output));
    assert(!x86_cpu_identity_plan(records, 0, 7, 90, &output));
    assert(!x86_cpu_identity_plan(records, 65, 7, 90, &output));
    assert(!memcmp(&before, &output, sizeof(output)));
    records[0] = (KERNEL_MADT_CPU){30, 11, 1, 0};
    records[1] = (KERNEL_MADT_CPU){90, 7, 1, 0};
    records[2] = (KERNEL_MADT_CPU){400, 1024, 2, 9};
    records[3] = (KERNEL_MADT_CPU){UINT32_MAX, UINT32_MAX, 0, 0};
    assert(x86_cpu_identity_plan(records, 4, 7, 90, &output));
    assert(output.Count == 2 && output.Offline == 1 && output.Ignored == 1 && output.Ready == 1);
    assert(output.Self == (uintptr_t)&output && !output.BspContext);
    assert(output.Cpus[0].Id == 7 && output.Cpus[0].Uid == 90 && !output.Cpus[0].Index);
    assert(output.Cpus[1].Id == 11 && output.Cpus[1].Uid == 30 && output.Cpus[1].Index == 1);
    before = output;
    assert(!x86_cpu_identity_plan(records, 4, 0, 90, &output));
    assert(!x86_cpu_identity_plan(records, 4, 7, 91, &output));
    assert(!x86_cpu_identity_plan(records, 4, 7, 90, (X86_CPU_IDENTITIES *)records));
    assert(!memcmp(&before, &output, sizeof(output)));
    records[2].Id = 7; assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output));
    records[2].Id = 1024; records[2].Uid = 30;
    assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output));
    records[2].Uid = 400; records[1].Flags = 2;
    assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output)); records[1].Flags = 1;
    records[0].Id = 255; assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output));
    records[0].Id = 256; records[0].Kind = 9;
    assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output));
    records[0].Id = 11; records[0].Flags = 3;
    assert(!x86_cpu_identity_plan(records, 4, 7, 90, &output));
    assert(!memcmp(&before, &output, sizeof(output)));
    for (uint32_t index = 0; index < 64; ++index) records[index] = (KERNEL_MADT_CPU){1000 + index, index * 3, 1, 9};
    for (uint32_t bsp = 0; bsp < 64; ++bsp) {
        assert(x86_cpu_identity_plan(records, 64, bsp * 3, 1000 + bsp, &output));
        assert(output.Count == 64 && !output.Offline && !output.Ignored && output.Cpus[0].Id == bsp * 3);
        uint32_t next = 1;
        for (uint32_t index = 0; index < 64; ++index) {
            if (index == bsp) continue;
            assert(output.Cpus[next].Index == next && output.Cpus[next].Id == index * 3 &&
                output.Cpus[next].Uid == 1000 + index); ++next;
        }
    }
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2); plan_private();
    uint64_t expected_if = !strcmp(argv[1], "if0") ? 0 : 0x200;
    model_if = expected_if;
    KERNEL_MADT_TOPOLOGY topology = {0}; X86_APIC_PROFILE profile = {0};
    topology.Ready = 1; topology.CpuCount = 3;
    topology.Cpus[0] = (KERNEL_MADT_CPU){30, 11, 1, 0};
    topology.Cpus[1] = (KERNEL_MADT_CPU){90, 7, 1, 0};
    topology.Cpus[2] = (KERNEL_MADT_CPU){400, 1024, 2, 9};
    profile.Ready = 1; profile.InitialId = 7; profile.Uid = 90;
    assert(!x86_cpu_identities() && !x86_cpu_identity_initialize(&topology, &profile, 48));
    assert(kernel_lock_bootstrap_initialize() && kernel_lock_bootstrap_promote());
    assert(!x86_cpu_identity_initialize(NULL, &profile, 48));
    trusted = 0; assert(!x86_cpu_identity_initialize(&topology, &profile, 48)); trusted = 1;
    model_bsp = 0; assert(!x86_cpu_identity_initialize(&topology, &profile, 48)); model_bsp = 1;
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    assert(kernel_lock_initialize(&lock, 10) && kernel_lock_acquire(&lock, context, &guard));
    assert(!x86_cpu_identity_initialize(&topology, &profile, 48));
    assert(kernel_lock_release(context, &guard) && model_if == expected_if);
    topology.Cpus[0].Id = 7; assert(!x86_cpu_identity_initialize(&topology, &profile, 48));
    topology.Cpus[0].Id = 11;
    assert(x86_cpu_identity_initialize(&topology, &profile, 48) && model_if == expected_if);
    const X86_CPU_IDENTITIES *snapshot = x86_cpu_identities();
    assert(snapshot && snapshot->Ready == 2 && snapshot->Count == 2 && snapshot->Offline == 1 &&
        snapshot->BspContext == (uintptr_t)context && snapshot->Cpus[0].Id == 7);
    X86_CPU_IDENTITIES before = *snapshot;
    KERNEL_MADT_TOPOLOGY valid_topology = topology; X86_APIC_PROFILE valid_profile = profile;
    memset(&topology, 0xff, sizeof(topology)); memset(&profile, 0xff, sizeof(profile));
    assert(!x86_cpu_identity_initialize(&topology, &profile, 48));
    assert(x86_cpu_identities() == snapshot && !memcmp(snapshot, &before, sizeof(before)));
    model_bsp = 0; assert(!x86_cpu_identities()); model_bsp = 1;
    X86_CPU_IDENTITIES *mutable = (X86_CPU_IDENTITIES *)snapshot;
    mutable->Count = 65; assert(!x86_cpu_identities()); *mutable = before;
    mutable->Cpus[1].Uid = 90; assert(!x86_cpu_identities()); *mutable = before;
    mutable->Cpus[63].Id = 1; assert(!x86_cpu_identities()); *mutable = before;
    mutable->BspContext = 0; assert(!x86_cpu_identities()); *mutable = before;
    mutable->Ready = 0;
    assert(!x86_cpu_identity_initialize(&valid_topology, &valid_profile, 48)); *mutable = before;
    assert(x86_cpu_identities() == snapshot && model_if == expected_if && !context->Depth);
    puts("PERCPU_IDENTITY_PASS: 64 BSP positions, sparse IDs, offline distinction, transactional plan, owned publication and IF; native input checks modeled");
    return 0;
}
