#include "arch/x86/kernel/cpu/percpu/admission/group.h"
#include "kernel/sync/internal.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static XAG_GROUP input;
static DWORD writer;
static uint32_t bsp = 1;
static uint64_t irq;
static volatile LONG readers_ready, readers_done;

// ====== platform_model ======

uint32_t ks_boot_cpu(void) { return GetCurrentThreadId() == writer && bsp; }

// ====== ks_irq_state ======

uint64_t ks_irq_state(void) { return GetCurrentThreadId() == writer ? irq : 0; }

// ====== ks_irq_save ======

uint64_t ks_irq_save(void) { uint64_t saved = irq; irq = 0; return saved; }

// ====== ks_irq_restore ======

void ks_irq_restore(uint64_t saved) { irq = saved; }

// ====== ks_fail_stop ======

__declspec(noreturn) void ks_fail_stop(const char *reason) { fprintf(stderr, "%s\n", reason); abort(); }

// ====== fixture_private ======

static void fixture_private(uint32_t count) {
    memset(&input, 0, sizeof(input));
    input.Header = (XAG_HEADER){1, sizeof(input), count, 48, 0, 0x200000, 0x0007040600070406ULL};
    for (uint32_t index = 0; index < count; ++index) {
        uint64_t local = 0xffffffff80010000ULL + index * 512ULL;
        uint64_t wait = 0xffffffff80020000ULL + index * 64ULL;
        input.Cpus[index] = (XAG_CPU){index, index + 7, local, wait,
            {{(int (*)(void *, uint32_t *))0xffffffff80100000ULL, (void *)(wait + 32), 0xffffff, 3579545},
                357955, 1000000}, {0x700000, 276, 0x608, 0xffffff, 1}};
    }
}

// ====== negative_private ======

static void negative_private(void) {
    assert(!xag_group() && !xag_match(1, 8, input.Cpus[1].Local));
    assert(!xag_publish(NULL) && !xag_publish((const XAG_GROUP *)(UINTPTR_MAX - 7)));
    bsp = 0; assert(!xag_publish(&input)); bsp = 1;
    irq = 0x200; assert(!xag_publish(&input)); irq = 0;
    for (uint32_t field = 0; field < 12; ++field) {
        XAG_CPU saved = input.Cpus[1]; XAG_HEADER header = input.Header;
        switch (field) {
        case 0: input.Header.Count = 65; break;
        case 1: input.Header.PhysicalBits = 53; break;
        case 2: input.Header.Self = (uint64_t)&input; break;
        case 3: input.Cpus[1].Index = 0; break;
        case 4: input.Cpus[1].ApicId = input.Cpus[0].ApicId; break;
        case 5: input.Cpus[1].Local = input.Cpus[0].Local + 64; break;
        case 6: input.Cpus[1].Wait = input.Cpus[0].Wait; break;
        case 7: input.Cpus[1].Expected.Clock.Context = input.Cpus[0].Expected.Clock.Context; break;
        case 8: input.Cpus[1].Expected.Clock.Read = (int (*)(void *, uint32_t *))input.Cpus[0].Local; break;
        case 9: input.Cpus[1].Source.Ready = 0; break;
        case 10: input.Cpus[1].Expected.MaxPolls = 0; break;
        default: input.Cpus[63].Index = 1; break;
        }
        assert(!xag_publish(&input) && !xag_group());
        input.Cpus[1] = saved; input.Header = header; input.Cpus[63] = (XAG_CPU){0};
    }
}

// ====== reader_private ======

static DWORD WINAPI reader_private(void *argument) {
    (void)argument;
    assert(!ks_boot_cpu() && !xag_publish(&input));
    InterlockedIncrement(&readers_ready);
    for (uint32_t poll = 0; poll < 10000000; ++poll) {
        const XAG_GROUP *group = xag_group();
        if (group) {
            assert(group->Header.Self == (uint64_t)group && group->Header.Count == input.Header.Count);
            for (uint32_t index = 0; index < group->Header.Count; ++index) {
                assert(!memcmp(&group->Cpus[index], &input.Cpus[index], sizeof(XAG_CPU)));
                assert(xag_match(index, index + 7, input.Cpus[index].Local) == &group->Cpus[index]);
            }
            InterlockedIncrement(&readers_done);
            return 0;
        }
        if (!(poll & 255)) SwitchToThread();
    }
    assert(0 && "publication deadline");
    return 1;
}

// ====== main ======

int main(int argc, char **argv) {
    uint32_t count = argc == 2 && !strcmp(argv[1], "full") ? 64 : 4;
    writer = GetCurrentThreadId();
    fixture_private(4);
    negative_private();
    fixture_private(count);
    HANDLE threads[4];
    for (uint32_t index = 0; index < 4; ++index) {
        threads[index] = CreateThread(NULL, 0, reader_private, NULL, 0, NULL);
        assert(threads[index]);
    }
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (InterlockedCompareExchange(&readers_ready, 0, 0) != 4 && GetTickCount64() < deadline) SwitchToThread();
    assert(InterlockedCompareExchange(&readers_ready, 0, 0) == 4);
    assert(xag_publish(&input) && !xag_publish(&input));
    assert(WaitForMultipleObjects(4, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (uint32_t index = 0; index < 4; ++index) assert(CloseHandle(threads[index]));
    assert(InterlockedCompareExchange(&readers_done, 0, 0) == 4);
    const XAG_GROUP *group = xag_group();
    XAG_CPU saved = group->Cpus[1];
    input.Cpus[1].Local = 0; input.Cpus[1].Expected.Ticks = 0;
    assert(!memcmp(&group->Cpus[1], &saved, sizeof(saved)) && !xag_publish(group));
    assert(!xag_match(64, 7, saved.Local) && !xag_match(1, 255, saved.Local) && !xag_match(1, 8, 0));
    printf("PASS: immutable group %u CPUs, negative retries, four concurrent acquire readers, no retained input alias\n", count);
    return 0;
}
