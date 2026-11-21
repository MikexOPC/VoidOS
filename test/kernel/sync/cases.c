#include "test.h"
static KERNEL_LOCK_CONTEXT handoff_context;
static KERNEL_LOCK_GUARD handoff_guard;
static uint32_t pauses;

// ====== handoff_private ======

static void handoff_private(void) {
    if (++pauses == 4)
        assert(kernel_lock_release(&handoff_context, &handoff_guard));
}

// ====== deadline_private ======

static void deadline_private(void) {
    ++pauses;
    assert(pauses <= 64);
}

// ====== fake_clock_private ======

static int fake_clock_private(void *context, uint32_t *value) {
    uint32_t *ticks = context;
    *value = (*ticks)++;
    return 1;
}

// ====== stopped_clock_private ======

static int stopped_clock_private(void *context, uint32_t *value) {
    (void)context;
    *value = 0;
    return 1;
}

// ====== failed_clock_private ======

static int failed_clock_private(void *context, uint32_t *value) {
    (void)context; (void)value;
    return 0;
}

// ====== bad_irq_clock_private ======

static int bad_irq_clock_private(void *context, uint32_t *value) {
    (void)context;
    test_set_if(0x200);
    *value = 0;
    return 1;
}

// ====== test_cases ======

void test_cases(void) {
    KERNEL_LOCK locks[9] = {0};
    KERNEL_LOCK_CONTEXT context = {0}, other = {0};
    KERNEL_LOCK_GUARD guards[9] = {0}, copy;
    assert(!kernel_lock_initialize(NULL, 1));
    assert(!kernel_lock_context_initialize(NULL, NULL));
    assert(!kernel_lock_initialize(&locks[0], 0));
    assert(!kernel_lock_initialize(&locks[0], 65536));
    for (uint32_t i = 0; i < 9; ++i) assert(kernel_lock_initialize(&locks[i], i + 1));
    assert(!kernel_lock_initialize(&locks[0], 1));
    assert(kernel_lock_context_initialize(&context, NULL));
    assert(kernel_lock_context_initialize(&other, NULL));
    assert(!kernel_lock_context_initialize(&context, NULL));
    for (uint32_t entry_if = 0; entry_if <= 0x200; entry_if += 0x200) {
        test_set_if(entry_if);
        assert(!kernel_lock_acquire(NULL, &context, &guards[0]) && ks_irq_state() == entry_if);
        assert(kernel_lock_acquire(&locks[0], &context, &guards[0]));
        assert(!ks_irq_state() && context.Depth == 1);
        assert(!kernel_lock_try(&locks[0], &other, &guards[1]));
        assert(ks_atomic_load_relaxed(&locks[0].Next.Value) == guards[0].Ticket + 1);
        assert(!kernel_lock_acquire(&locks[0], &context, &guards[1]));
        copy = guards[0];
        assert(!kernel_lock_release(&context, &copy));
        assert(!kernel_lock_release(&other, &guards[0]));
        assert(kernel_lock_acquire(&locks[1], &context, &guards[1]));
        assert(!kernel_lock_release(&context, &guards[0]));
        assert(kernel_lock_release(&context, &guards[1]) && !ks_irq_state());
        uint64_t saved = guards[0].SavedIf;
        guards[0].SavedIf ^= 0x200;
        assert(!kernel_lock_release(&context, &guards[0]));
        guards[0].SavedIf = saved;
        assert(kernel_lock_release(&context, &guards[0]) && ks_irq_state() == entry_if);
        assert(!kernel_lock_release(&context, &guards[0]) && ks_irq_state() == entry_if);
    }
    test_set_if(0x200);
    for (uint32_t i = 0; i < 8; ++i) assert(kernel_lock_acquire(&locks[i], &context, &guards[i]));
    assert(!kernel_lock_acquire(&locks[8], &context, &guards[8]));
    for (uint32_t i = 8; i; --i) assert(kernel_lock_release(&context, &guards[i - 1]));
    assert(ks_irq_state() == 0x200 && !context.Depth);
    assert(!kernel_lock_acquire(&locks[0], &context, (KERNEL_LOCK_GUARD *)&locks[0]));
    assert(!kernel_lock_acquire(&locks[0], &context, (KERNEL_LOCK_GUARD *)&context));
    assert(!kernel_lock_acquire(&locks[0], &context, (KERNEL_LOCK_GUARD *)((char *)&guards[0] + 1)));
    copy = guards[0];
    assert(kernel_lock_acquire(&locks[0], &context, &guards[0]));
    KERNEL_LOCK_GUARD stale = guards[0];
    assert(kernel_lock_release(&context, &guards[0]));
    assert(kernel_lock_acquire(&locks[0], &context, &guards[0]));
    KERNEL_LOCK_GUARD live = guards[0];
    guards[0] = stale;
    assert(!kernel_lock_release(&context, &guards[0]));
    guards[0] = live;
    assert(kernel_lock_release(&context, &guards[0]));
    locks[0].Rank = 2;
    assert(kernel_lock_acquire(&locks[0], &context, &guards[0]));
    locks[0].Rank = 3;
    assert(!kernel_lock_release(&context, &guards[0]));
    locks[0].Rank = 2;
    assert(kernel_lock_release(&context, &guards[0]));
    locks[0].Rank = 1;
    context.NextToken = UINT64_MAX - 1;
    assert(kernel_lock_acquire(&locks[0], &context, &guards[0]));
    assert(kernel_lock_release(&context, &guards[0]) && context.NextToken == UINT64_MAX);
    assert(!kernel_lock_acquire(&locks[0], &context, &guards[0]));
    context.NextToken = 100;
    ks_atomic_store_relaxed(&locks[0].Next.Value, UINT64_MAX - 1);
    ks_atomic_store_release(&locks[0].Serving.Value, UINT64_MAX - 1);
    assert(kernel_lock_try(&locks[0], &context, &guards[0]));
    assert(guards[0].Ticket == UINT64_MAX - 1 && kernel_lock_release(&context, &guards[0]));
    assert(!kernel_lock_try(&locks[0], &context, &guards[0]) && ks_irq_state() == 0x200);
    uint32_t ticks = UINT32_MAX - 2;
    KERNEL_LOCK_WAIT wait = {{fake_clock_private, &ticks, UINT32_MAX, 1000}, 100, 100};
    KERNEL_LOCK lock = {0};
    KERNEL_LOCK_CONTEXT waiting = {0};
    KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 1));
    assert(kernel_lock_context_initialize(&handoff_context, NULL));
    assert(kernel_lock_context_initialize(&waiting, &wait));
    assert(!kernel_lock_acquire(&lock, &waiting, (KERNEL_LOCK_GUARD *)&wait));
    test_set_if(0);
    assert(kernel_lock_acquire(&lock, &handoff_context, &handoff_guard));
    test_set_pause(handoff_private);
    assert(kernel_lock_acquire(&lock, &waiting, &guard) && guard.Ticket == 1);
    assert(pauses == 4 && !handoff_context.Depth);
    assert(ticks == UINT32_MAX);
    test_set_pause(NULL);
    assert(kernel_lock_release(&waiting, &guard));
    assert(!ks_irq_state());
    (void)copy;
    puts("PASS: initialization, IF, nesting, ranks, alias, stale guards, FIFO handoff and exhaustion");
}

// ====== test_fatal ======

void test_fatal(const char *name) {
    KERNEL_LOCK first = {0}, second = {0};
    KERNEL_LOCK_CONTEXT owner = {0}, waiter = {0};
    KERNEL_LOCK_GUARD guard = {0}, other = {0};
    uint32_t ticks = 0;
    KERNEL_LOCK_WAIT wait = {{fake_clock_private, &ticks, UINT32_MAX, 1000}, 2, 4};
    if (!strcmp(name, "deadline")) {
        wait.MaxPolls = 128;
        test_set_pause(deadline_private);
    }
    if (!strcmp(name, "stopped")) wait.Clock.Read = stopped_clock_private;
    if (!strcmp(name, "failed")) wait.Clock.Read = failed_clock_private;
    if (!strcmp(name, "clock-if")) wait.Clock.Read = bad_irq_clock_private;
    assert(kernel_lock_initialize(&first, 1) && kernel_lock_initialize(&second, 2));
    assert(kernel_lock_context_initialize(&owner, NULL));
    assert(kernel_lock_context_initialize(&waiter, !strcmp(name, "no-clock") ? NULL : &wait));
    test_set_if(0x200);
    assert(kernel_lock_acquire(&first, &owner, &guard));
    test_watch_fatal(&first);
    if (!strcmp(name, "release-if")) {
        test_set_if(0x200);
        (void)kernel_lock_release(&owner, &guard);
    }
    else if (!strcmp(name, "nested-if")) {
        test_set_if(0x200);
        (void)kernel_lock_acquire(&second, &owner, &other);
    }
    else {
        (void)kernel_lock_acquire(&first, &waiter, &other);
    }
    assert(!"fail-stop returned");
}
