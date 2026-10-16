#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "kernel/scheduler/payload.h"

static X86_PS2_SESSION owner;
static KERNEL_MADT_TOPOLOGY topology;
static X86_APIC_CAPABILITIES caps;
static X86_APIC_PROFILE profile;
static BOOT_U32 regs[256], selector, calls, fail, borrowed, checks;
static unsigned char bytes[64];
static BOOT_U32 head, size, port_reads, bad_status, pushed, push_result, eoi, eoi_result;
const unsigned char x86_scheduler_irq_compute_begin[1] = {0};
const unsigned char x86_scheduler_irq_compute_end[1] = {0};
#define CHECK(e) do { ++checks; assert(e); } while (0)

// ====== checked MMIO and IRQ boundary mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &owner; }
int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *output)
{
    if (++calls == fail) return 0;
    CHECK(borrowed && (offset == 0 || offset == 0x10));
    *output = offset ? regs[selector] : selector; return 1;
}
int x86_apic_control_write(BOOT_U32 offset, BOOT_U32 value)
{
    if (++calls == fail) return 0;
    CHECK(borrowed && (offset == 0 || offset == 0x10));
    if (offset) regs[selector] = value;
    else { CHECK(value < 256); selector = value; }
    return 1;
}
int x86_apic_borrow(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*operation)(void *), void *context)
{
    (void)window;
    CHECK(!borrowed && physical == 0xfec00000 && cache == 24 && writable);
    borrowed = 1;
    int result = operation(context);
    borrowed = 0; return result;
}
unsigned char x86_read_port(unsigned short port)
{
    ++port_reads;
    CHECK(port == 0x60 || port == 0x64);
    if (port == 0x64) return (unsigned char)((head < size ? 1 : 0) | bad_status);
    CHECK(head < size); return bytes[head++];
}
BOOT_U32 kernel_scheduler_payload_push(BOOT_U32 event, BOOT_U64 source, BOOT_U64 generation, BOOT_U32 byte)
{
    CHECK(((event == 4 && source == (BOOT_U64)&owner && generation == 7) ||
        (event == 6 && source == (BOOT_U64)&owner.Mouse && generation == 8)) && byte < 256);
    ++pushed; return push_result;
}
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *mapping, BOOT_U32 vector)
{ (void)mapping; CHECK(vector == 0x41 || vector == 0x42); ++eoi; return (int)eoi_result; }
uint32_t x86_scheduler_index(void) { return 1; }
int x86_scheduler_task_valid(void) { return 1; }
uint64_t x86_task_stack_base(uint32_t index) { (void)index; return 0x700000150000; }
int x86_scheduler_code_valid(uint64_t address) { (void)address; return 1; }

// ====== validated MADT fixture ======

static void reset(void)
{
    memset(&owner,0,sizeof(owner)); memset(&topology,0,sizeof(topology));
    memset(&caps,0,sizeof(caps)); memset(regs,0,sizeof(regs));
    topology.Physical = 0x200000; topology.Length = 64; topology.Revision = 1;
    topology.Lapic = 0xfee00000; topology.Ready = 1;
    topology.CpuCount = 1; topology.Cpus[0].Flags = 1;
    topology.IoCount = 1; topology.Io[0].Address = 0xfec00000;
    caps.Ready = 1; caps.IoCount = 1;
    caps.Io[0].Address = 0xfec00000; caps.Io[0].Version = 0x170020;
    caps.Io[0].Inputs = 24; caps.Io[0].End = 24;
    profile.CacheBits = 24; profile.InitialId = 0;
    owner.Env.Topology = &topology; owner.Env.Capabilities = &caps; owner.Env.Profile = &profile;
    selector = 7; regs[1] = 0x170020; regs[0x12] = 0x10055; regs[0x13] = 0x1000000;
    calls = fail = borrowed = head = size = port_reads = bad_status = pushed = eoi = 0;
    push_result = eoi_result = 1;
}

// ====== route discovery rejects aliases conflicts and inconsistent capacities ======

static void plan_tests(void)
{
    reset();
    CHECK(x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    CHECK(owner.Route.Pin == 1 && owner.Route.Bits == 0x41 && !owner.Route.Destination);
    for (BOOT_U32 flags = 0; flags < 32; ++flags) {
        reset(); topology.Length += 10; topology.IsoCount = 1;
        topology.Iso[0].Source = 1; topology.Iso[0].Gsi = 20; topology.Iso[0].Flags = flags;
        int valid = flags < 16 && (flags & 3) != 2 && ((flags >> 2) & 3) != 2;
        CHECK(x86_ps2_route_plan(&topology,&caps,48,&owner.Route) == valid);
        if (valid) CHECK(owner.Route.Pin == 20 && owner.Route.Gsi == 20);
    }
    reset(); caps.IoCount = 17;
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    reset(); caps.Io[0].Inputs = 25;
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    reset(); caps.LapicId = 256;
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    reset(); topology.IsoCount = 1; topology.Length += 10;
    topology.Iso[0].Source = 1; topology.Iso[0].Gsi = 2;
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    reset(); topology.NmiCount = 1; topology.Length += 8;
    topology.Nmi[0].Kind = 3; topology.Nmi[0].Value = 1;
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    reset();
    CHECK(!x86_ps2_route_plan(&topology,&caps,48,(X86_PS2_ROUTE *)&caps));
    CHECK(!x86_ps2_route_plan(&topology,&caps,0,&owner.Route));
    CHECK(!calls && !port_reads);
}

// ====== route ownership rollback and selector restoration ======

static void route_tests(void)
{
    BOOT_U32 acquire_calls;
    reset(); CHECK(x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    CHECK(x86_ps2_route_apply(&owner,1) && owner.RouteOwned && selector == 7);
    acquire_calls = calls;
    CHECK(regs[0x12] == 0x10041 && !regs[0x13]);
    CHECK(x86_ps2_route_apply(&owner,0) && regs[0x12] == 0x41 && selector == 7);
    CHECK(x86_ps2_route_apply(&owner,1) && regs[0x12] == 0x10041);
    CHECK(!x86_ps2_route_apply(&owner,2) && owner.RouteOwned);
    owner.Quiet = 1;
    CHECK(x86_ps2_route_apply(&owner,2) && !owner.RouteOwned && regs[0x12] == 0x10055 && regs[0x13] == 0x1000000);
    for (BOOT_U32 step = 1; step <= acquire_calls; ++step) {
        reset(); CHECK(x86_ps2_route_plan(&topology,&caps,48,&owner.Route)); fail = step;
        CHECK(!x86_ps2_route_apply(&owner,1) && !borrowed);
        if (owner.RouteOwned) {
            fail = 0; owner.Quiet = 1;
            CHECK(x86_ps2_route_apply(&owner,2) && !owner.RouteOwned);
        }
    }
    reset(); CHECK(x86_ps2_route_plan(&topology,&caps,48,&owner.Route));
    regs[0x12] = 0x41;
    CHECK(!x86_ps2_route_apply(&owner,1) && !owner.RouteOwned && regs[0x12] == 0x41);
}

// ====== bounded ISR no decoding allocation or logging ======

static void interrupt_tests(void)
{
    for (BOOT_U32 count = 0; count <= 33; ++count) {
        reset(); owner.Active = 1; owner.Binding = 7; size = count;
        memset(bytes,0x1c,sizeof(bytes));
        CHECK(x86_ps2_handle() == (int)(count <= 32));
        CHECK(head == (count > 32 ? 32 : count) && pushed == head);
        CHECK(owner.Bytes == head && owner.Irqs == 1 && owner.Ready == (BOOT_U32)(count != 0));
        CHECK(eoi == (count <= 32 ? 1U : 0U));
    }
    #ifdef KERNEL_PS2_MOUSE
    for (BOOT_U32 vector = 0x41; vector <= 0x42; ++vector) {
        for (BOOT_U32 auxiliary = 0; auxiliary <= 1; ++auxiliary) {
            reset(); owner.Active = 1; owner.Binding = 7; owner.Mouse.Binding = 8;
            size = 3; bad_status = auxiliary << 5;
            CHECK(vector == 0x41 ? x86_ps2_handle() : x86_ps2_mouse_handle());
            CHECK(head == 3 && pushed == 3 && eoi == 1);
            CHECK(owner.Bytes == (auxiliary ? 0U : 3U) && owner.Mouse.Bytes == (auxiliary ? 3U : 0U));
            CHECK(owner.Irqs == (BOOT_U32)(vector == 0x41) && owner.Mouse.Irqs == (BOOT_U32)(vector == 0x42));
        }
    }
    for (BOOT_U32 bit = 6; bit <= 7; ++bit)
#else
    for (BOOT_U32 bit = 5; bit <= 7; ++bit)
#endif
    {
        reset(); owner.Active = 1; size = 1; bad_status = 1U << bit;
        CHECK(!x86_ps2_handle() && owner.Error && !head && !pushed && !eoi);
    }
    for (BOOT_U32 push = 0; push <= 2; ++push) {
        reset(); owner.Active = 1; owner.Binding = 7; size = 1; push_result = push;
        CHECK(x86_ps2_handle() == (int)(push != 0));
        CHECK(owner.Error == (BOOT_U32)(push == 0));
    }
    reset(); owner.Active = owner.Quiet = 1; size = 2;
    CHECK(x86_ps2_handle() && head == 2 && !pushed && !owner.Bytes);
    reset(); CHECK(!x86_ps2_handle() && !port_reads);
    owner.Active = 1; owner.Irqs = ~0U;
    CHECK(!x86_ps2_handle() && !port_reads);
    reset(); owner.Active = 1; eoi_result = 0;
    CHECK(!x86_ps2_handle() && eoi == 1);
}

// ====== mouse_route_tests ======

static void mouse_route_tests(void)
{
    reset();
    CHECK(x86_ps2_route_plan_source(&topology,&caps,48,12,&owner.Mouse.Route));
    CHECK(owner.Mouse.Route.Pin == 12 && owner.Mouse.Route.Bits == 0x42);
    regs[0x28] = 0x10066; regs[0x29] = 0x2000000;
    CHECK(x86_ps2_mouse_route_apply(&owner,1) && owner.Mouse.RouteOwned && !owner.RouteOwned);
    CHECK(regs[0x28] == 0x10042 && !regs[0x29] && selector == 7);
    CHECK(x86_ps2_mouse_route_apply(&owner,0) && regs[0x28] == 0x42);
    CHECK(!x86_ps2_mouse_route_apply(&owner,2)); owner.Quiet = 1;
    CHECK(x86_ps2_mouse_route_apply(&owner,2) && !owner.Mouse.RouteOwned &&
        regs[0x28] == 0x10066 && regs[0x29] == 0x2000000);
    for (BOOT_U32 flags = 0; flags < 32; ++flags) {
        reset(); topology.Length += 10; topology.IsoCount = 1;
        topology.Iso[0].Source = 12; topology.Iso[0].Gsi = 20; topology.Iso[0].Flags = flags;
        int valid = flags < 16 && (flags & 3) != 2 && ((flags >> 2) & 3) != 2;
        CHECK(x86_ps2_route_plan_source(&topology,&caps,48,12,&owner.Mouse.Route) == valid);
        if (valid) CHECK(owner.Mouse.Route.Pin == 20);
    }
    reset(); topology.Length += 10; topology.IsoCount = 1;
    topology.Iso[0].Source = 12; topology.Iso[0].Gsi = 1;
    CHECK(!x86_ps2_route_plan_source(&topology,&caps,48,12,&owner.Mouse.Route));
    reset();
    CHECK(!x86_ps2_route_plan_source(&topology,&caps,48,11,&owner.Mouse.Route));
}

// ====== runtime_host_main ======

int main(void)
{
    plan_tests(); route_tests(); mouse_route_tests(); interrupt_tests();
    printf("PASS: PS2 runtime %u checks, actual MADT validator, route/IRQ hardware MOCK\n",checks);
    return 0;
}
