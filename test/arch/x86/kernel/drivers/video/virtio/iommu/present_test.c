#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "arch/x86/kernel/drivers/video/virtio/iommu/present/state.h"

// G6 O6: the asynchronous present as an object, on the host. The VirtIO queue, transport, surface
// authority and drawing target are mocks; the clock (kernel/time) and the damage (lib/geometry)
// are the real modules. Covers R1-A merge and single update in flight, R2 SLOW and timeout,
// R3-A recovery spacing and limit, non-recoverable faults, close and the target check.
#define CHECK(expression) do { ++checks; assert(expression); } while (0)
#define WIDTH  64U
#define HEIGHT 32U

static uint32_t checks, now, resets, ends, negotiates, posts, settles, unbinds, closes, slows_logged;
static int poll_result, post_ok = 1, negotiate_ok = 1;
static uint32_t poll_fault, last_phase, last_x, last_y, last_w, last_h;
static const X86_FB_TARGET *current;
static X86_SURFACE_MAPPING surface;
static uint32_t pixels[WIDTH * HEIGHT];
static unsigned char arena_bytes[4 * 4096];

// ====== clock_read ======

static int clock_read(void *context, uint32_t *value) { (void)context; *value = now & 0xffffff; return 1; }

// ====== transport and queue mocks ======

int x86_vgpu_transport_open(X86_VGPU_MAPPING *owner, const KERNEL_ACPI_DIRECTORY *directory,
    X86_VGPU_TRANSPORT *transport, KERNEL_VGPU_IO *io)
{
    (void)owner; (void)directory;
    transport->Self = (BOOT_U64)transport;
    io->Clock = clock_read; io->Context = 0; io->Mask = 0xffffff; io->Samples = 8;
    return 1;
}

int x86_vgpu_transport_end(const X86_VGPU_TRANSPORT *transport) { (void)transport; ++ends; return 1; }

int x86_vgpu_queue_open(X86_VGPU_TRANSPORT *transport, X86_VGPU_QUEUE_TRANSPORT *native, VGPU_QUEUE_IO_CONTEXT *io)
{ (void)transport; (void)io; native->Self = (BOOT_U64)native; return 1; }

volatile unsigned char *x86_dma_arena_bytes(X86_DMA_ARENA *owner) { (void)owner; return arena_bytes; }
BOOT_U64 x86_vgpu_iommu_device(BOOT_U64 physical, BOOT_U64 bytes, BOOT_U32 access) { (void)bytes; (void)access; return physical | 1; }
int kernel_vgpu_reset(const KERNEL_VGPU_IO *io, KERNEL_VGPU_NEGOTIATION *result) { (void)io; (void)result; ++resets; return 1; }
int kernel_vgpu_negotiate(const KERNEL_VGPU_IO *io, KERNEL_VGPU_NEGOTIATION *result) { (void)io; (void)result; ++negotiates; return negotiate_ok; }

int kernel_vgpu_queue_start(VGPU_QUEUE *queue, const VGPU_QUEUE_IO_CONTEXT *io, const VGPU_BUFFERS *buffers)
{ (void)io; (void)buffers; queue->Width = WIDTH; queue->Height = HEIGHT; return 1; }

int kernel_vgpu_queue_exchange(VGPU_QUEUE *queue) { (void)queue; return 1; }
uint64_t kernel_vgpu_surface_bytes(uint32_t width, uint32_t height) { return (uint64_t)width * height * 4; }

int kernel_vgpu_resource_open(VGPU_QUEUE *queue, VGPU_RESOURCE *resource, uint64_t physical, uint64_t device,
    uint32_t width, uint32_t height)
{ (void)queue; (void)resource; (void)physical; (void)device; return width == WIDTH && height == HEIGHT; }

int kernel_vgpu_resource_close(VGPU_QUEUE *queue, VGPU_RESOURCE *resource) { (void)queue; (void)resource; ++closes; return 1; }
int kernel_vgpu_scanout_bind(VGPU_QUEUE *queue, VGPU_RESOURCE *resource) { (void)queue; (void)resource; return 1; }
int kernel_vgpu_scanout_unbind(VGPU_QUEUE *queue, VGPU_RESOURCE *resource) { (void)queue; (void)resource; ++unbinds; return 1; }
int kernel_vgpu_scanout_settle(const VGPU_QUEUE *queue, VGPU_RESOURCE *resource) { (void)queue; (void)resource; ++settles; return 1; }

int kernel_vgpu_scanout_post(VGPU_QUEUE *queue, VGPU_RESOURCE *resource, uint32_t phase,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    (void)queue; (void)resource;
    ++posts; last_phase = phase; last_x = x; last_y = y; last_w = width; last_h = height;
    return post_ok;
}

// The queue poll: 1 completed, 0 pending, -1 fault with the scripted fault code.
int kernel_vgpu_queue_poll(VGPU_QUEUE *queue, uint32_t limit)
{
    CHECK(limit == PRESENT_LIMIT_PM);
    if (poll_result < 0) queue->Fault = poll_fault;
    return poll_result;
}

// ====== target, surface and log mocks ======

const X86_FB_TARGET *x86_fb_target(void) { return current; }

int x86_fb_target_redirect(const X86_FB_TARGET *expected, const X86_FB_TARGET *next)
{
    if (current != expected) return 0;
    current = next;
    return 1;
}

const X86_SURFACE_AUTH *x86_surface_owner(void) { return &surface.Authority; }
int x86_vgpu_surface_check(const X86_SURFACE_AUTH *auth) { return auth == &surface.Authority; }

int kernel_log(uint32_t level, const char *message)
{
    (void)level;
    if (!strcmp(message, "KERNEL_VGPU_PRESENT_SLOW")) ++slows_logged;
    return 1;
}

int kernel_text(const char *text) { return text != 0; }
int kernel_number(BOOT_U64 number) { (void)number; return 1; }
void kernel_latency_add(KERNEL_LATENCY *owner, uint64_t ticks) { ++owner->Count; owner->Total += ticks; }
int kernel_latency_report(const char *name, const KERNEL_LATENCY *owner) { (void)owner; return name != 0; }

// ====== open_present ======

static void open_present(X86_VGPU_PRESENT *self)
{
    X86_VGPU_MAPPING gpu = {0};
    X86_DMA_ARENA arena = {0};

    memset(self, 0, sizeof(*self));
    current = 0;
    surface.Authority.Bytes = sizeof(pixels);
    surface.Authority.Data.Base = 0x40000000;
    arena.Authority.Data.Base = 0x50000000;
    CHECK(x86_vgpu_present_open(self, &gpu, 0, &arena, &surface));
    self->Pixels = pixels;
    CHECK(self->Presenting && current == &self->Target && x86_vgpu_present_redraw(self) && !x86_vgpu_present_redraw(self));
}

// ====== merge_cases ======

// R1-A: one update in flight; damage arriving meanwhile is merged and presented next as one box.
static void merge_cases(void)
{
    X86_VGPU_PRESENT present;

    open_present(&present);
    CHECK(!x86_vgpu_present_busy(&present));
    CHECK(x86_vgpu_present_rect(&present, 0, 0, 0, 5) && posts == 0);

    poll_result = 0;
    CHECK(x86_vgpu_present_rect(&present, 8, 4, 8, 16) && posts == 1 && last_phase == 0);
    CHECK(last_x == 8 && last_y == 4 && last_w == 8 && last_h == 16 && present.Phase == 1);
    CHECK(x86_vgpu_present_rect(&present, 40, 2, 8, 16) && x86_vgpu_present_rect(&present, 16, 10, 8, 16) && posts == 1);
    CHECK(x86_vgpu_present_busy(&present));

    poll_result = 1;
    CHECK(x86_vgpu_present_poll(&present));
    // TRANSFER and FLUSH of the first box completed, then the merged envelope is posted.
    CHECK(settles == 1 && present.Presents == 2 && last_phase == 1);
    CHECK(present.FlyX == 16 && present.FlyY == 2 && present.FlyW == 32 && present.FlyH == 24);
    CHECK(x86_vgpu_present_poll(&present) && settles == 2 && !present.Phase && !x86_vgpu_present_busy(&present));
    CHECK(present.PresentTicks.Count == 2 && present.PresentArea.Total == 8U * 16U + 32U * 24U);
    CHECK(present.Target.Valid(&present.Target));
    CHECK(x86_vgpu_present_close(&present) && unbinds == 1 && closes == 1 && current == 0 && !present.Transport.Self);
}

// ====== slow_timeout_recovery_cases ======

// R2 SLOW once past 100 ms, timeout fallback, R3-A recovery 5 s apart, at most RECOVER_TRIES.
static void slow_timeout_recovery_cases(void)
{
    X86_VGPU_PRESENT present;
    uint32_t resets_before;

    open_present(&present);
    poll_result = 0;
    CHECK(x86_vgpu_present_rect(&present, 0, 0, 8, 16) && present.Phase == 1);
    now += PRESENT_SLOW_PM - 1;
    CHECK(x86_vgpu_present_poll(&present) && present.Slows == 0);
    now += 2;
    CHECK(x86_vgpu_present_poll(&present) && x86_vgpu_present_poll(&present) && present.Slows == 1 && slows_logged == 1);

    for (uint32_t round = 0; round <= RECOVER_TRIES; ++round) {
        resets_before = resets;
        poll_result = -1; poll_fault = VGPU_QUEUE_TIME;
        CHECK(x86_vgpu_present_poll(&present));
        CHECK(!present.Presenting && present.Timeouts == round + 1 && resets == resets_before + 1 && current == 0);
        CHECK(x86_vgpu_present_redraw(&present));
        if (round == RECOVER_TRIES) break;

        // Spacing: nothing before 5 s, then one bring-up with the cleared surface presented whole.
        poll_result = 1;
        CHECK(x86_vgpu_present_busy(&present));
        for (uint32_t second = 0; second < 4; ++second) {
            now += KERNEL_PM_TIMER_HZ;
            CHECK(x86_vgpu_present_poll(&present) && !present.Presenting);
        }
        pixels[0] = pixels[WIDTH * HEIGHT - 1] = 0xffffffffU;
        now += KERNEL_PM_TIMER_HZ + 1;
        poll_result = 0;
        CHECK(x86_vgpu_present_poll(&present) && present.Presenting && present.Recovered == round + 1);
        CHECK(!pixels[0] && !pixels[WIDTH * HEIGHT - 1] && current == &present.Target);
        CHECK(present.FlyX == 0 && present.FlyY == 0 && present.FlyW == WIDTH && present.FlyH == HEIGHT && present.Phase == 1);
        CHECK(x86_vgpu_present_redraw(&present));
    }

    // Limit reached: no further recovery, the console stops flushing for it.
    CHECK(!x86_vgpu_present_busy(&present));
    now += 10U * KERNEL_PM_TIMER_HZ;
    CHECK(x86_vgpu_present_poll(&present) && !present.Presenting && present.Recoveries == RECOVER_TRIES);
    CHECK(x86_vgpu_present_close(&present));
}

// ====== fault_cases ======

// A response fault or a failed post is not recoverable; a failed bring-up stays on GOP.
static void fault_cases(void)
{
    X86_VGPU_PRESENT present;

    open_present(&present);
    poll_result = 0;
    CHECK(x86_vgpu_present_rect(&present, 0, 0, 8, 16) && present.Phase == 1);
    poll_result = -1; poll_fault = VGPU_QUEUE_RESPONSE;
    CHECK(x86_vgpu_present_poll(&present) && !present.Presenting && !present.Recoverable && present.Failures == 1);
    CHECK(!x86_vgpu_present_busy(&present));
    CHECK(x86_vgpu_present_close(&present));

    open_present(&present);
    post_ok = 0;
    CHECK(x86_vgpu_present_rect(&present, 0, 0, 8, 16) && !present.Presenting && !present.Recoverable);
    post_ok = 1;
    uint32_t posted = posts;

    CHECK(x86_vgpu_present_rect(&present, 0, 0, 8, 16) && posts == posted);
    CHECK(x86_vgpu_present_close(&present));

    open_present(&present);
    CHECK(!x86_vgpu_present_open(&present, 0, 0, 0, &surface));
    CHECK(x86_vgpu_present_abandon(&present) && !present.Presenting && current == 0);
    CHECK(x86_vgpu_present_abandon(&present));
    CHECK(x86_vgpu_present_close(&present));

    // A display larger than the surface (geometry A) keeps GOP.
    X86_VGPU_PRESENT small = {0};
    X86_VGPU_MAPPING gpu = {0};
    X86_DMA_ARENA arena = {0};

    surface.Authority.Bytes = 16;
    CHECK(x86_vgpu_present_open(&small, &gpu, 0, &arena, &surface) && !small.Presenting && current == 0);
    CHECK(x86_vgpu_present_close(&small));
    surface.Authority.Bytes = sizeof(pixels);

    negotiate_ok = 0;
    CHECK(x86_vgpu_present_open(&small, &gpu, 0, &arena, &surface) && !small.Presenting);
    negotiate_ok = 1;
    CHECK(x86_vgpu_present_close(&small));
}

// ====== target_cases ======

// The target check finds its object from the embedded target and refuses copies or a foreign owner.
static void target_cases(void)
{
    X86_VGPU_PRESENT present;

    open_present(&present);
    X86_FB_TARGET copy = present.Target;

    CHECK(present.Target.Valid(&present.Target) && !present.Target.Valid(&copy) && !present.Target.Valid(0));
    present.Bound = 0;
    CHECK(!present.Target.Valid(&present.Target));
    present.Bound = &surface;
    CHECK(x86_vgpu_present_close(&present));
}

// ====== main ======

int main(void)
{
    merge_cases();
    slow_timeout_recovery_cases();
    fault_cases();
    target_cases();
    printf("PASS: VirtIO present object %u checks; queue, transport and surface MOCK\n", checks);
    return 0;
}
