#include "kernel/bootstrap/memory.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static KERNEL_BOOTSTRAP_MEMORY memory;
__declspec(align(4096)) static unsigned char arena_bytes[65536];
static jmp_buf terminal;
static unsigned int checks, calls, fail_call, physical_claims, allocations;
static const char *panic_message;

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Bootstrap check %u failed\n",checks); exit(1); }
}

// ====== step_private ======

static int step_private(void) { return ++calls != fail_call; }

// ====== existing_api_mocks ======

// ====== x86_physical_bits_mock ======

BOOT_U32 x86_physical_bits(void) { return 40; }
// ====== kernel_physical_initialize_mock ======

int kernel_physical_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
    BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits)
{
    check_private(owner == &memory.Physical && map == memory.Context.MemoryMap &&
        bytes == 48 && stride == 48 && bits == 40);
    if (!step_private()) return 0;
    owner->FreePages = 1024; return 1;
}
// ====== kernel_physical_validate_mock ======

int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner)
{ check_private(owner == &memory.Physical); return step_private(); }
// ====== x86_page_window_initialize_mock ======

int x86_page_window_initialize(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    check_private(window == &memory.Window && pool == &memory.Physical && root == 0x1000);
    if (!step_private()) return 0;
    window->Ready = 1; return 1;
}
// ====== x86_page_window_idle_valid_mock ======

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *window)
{ check_private(window == &memory.Window && window->Ready && !window->Active); return step_private(); }
// ====== x86_region_initialize_mock ======

int x86_region_initialize(X86_REGION_MAPPER *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    check_private(owner == &memory.Regions && pool == &memory.Physical && root == 0x1000);
    if (!step_private()) return 0;
    owner->Ready = 1; return 1;
}
// ====== x86_region_acquire_mock ======

int x86_region_acquire(X86_REGION_MAPPER *owner, BOOT_U64 pages, BOOT_U64 alignment, X86_REGION_HANDLE *handle)
{
    check_private(owner == &memory.Regions && pages == 16 && alignment == 16 &&
        handle == &memory.Handles[1] && !physical_claims);
    if (!step_private()) return 0;
    ++physical_claims; memory.Physical.FreePages -= pages;
    handle->Pages = pages; handle->Virtual = (BOOT_U64)(uintptr_t)arena_bytes;
    return 1;
}
// ====== x86_region_data_mock ======

volatile unsigned char *x86_region_data(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle)
{
    check_private(owner == &memory.Regions && handle == &memory.Handles[1]);
    return step_private() ? arena_bytes : NULL;
}
// ====== x86_region_validate_mock ======

int x86_region_validate(const X86_REGION_MAPPER *owner)
{ check_private(owner == &memory.Regions && owner->Ready); return step_private(); }
// ====== x86_object_arena_attach_mock ======

int x86_object_arena_attach(X86_OBJECT_ARENA *owner, X86_REGION_MAPPER *mapper, const X86_REGION_HANDLE *handle)
{
    check_private(owner == &memory.Arena && mapper == &memory.Regions && handle == &memory.Handles[1]);
    if (!step_private()) return 0;
    owner->Ready = 1; return 1;
}
// ====== x86_object_arena_allocate_mock ======

int x86_object_arena_allocate(X86_OBJECT_ARENA *owner, BOOT_U64 bytes, BOOT_U64 alignment, KERNEL_OBJECT_HANDLE *handle)
{
    check_private(owner == &memory.Arena && allocations < 2);
    if (!step_private()) return 0;
    ++allocations; handle->Offset = (owner->Objects.Used + alignment - 1) & ~(alignment - 1);
    handle->Bytes = bytes; owner->Objects.Used += bytes; ++owner->Objects.Count;
    return 1;
}
// ====== x86_object_arena_data_mock ======

unsigned char *x86_object_arena_data(const X86_OBJECT_ARENA *owner, const KERNEL_OBJECT_HANDLE *handle)
{
    check_private(owner == &memory.Arena && handle->Offset <= sizeof(arena_bytes) - handle->Bytes);
    return step_private() ? arena_bytes + handle->Offset : NULL;
}
// ====== x86_object_arena_validate_mock ======

int x86_object_arena_validate(const X86_OBJECT_ARENA *owner)
{ check_private(owner == &memory.Arena); return step_private(); }
// ====== kernel_text_mock ======

int kernel_text(const char *text) { check_private(text != NULL); return 1; }
// ====== kernel_section_mock ======

int kernel_section(const char *name) { check_private(name != NULL); return 1; }
// ====== kernel_number_mock ======

int kernel_number(BOOT_U64 value) { (void)value; return 1; }
// ====== kernel_log_mock ======

int kernel_log(unsigned int level, const char *message)
{ check_private(level == 0 && message != NULL); return 1; }
__declspec(noreturn) void kernel_panic(const char *message)
{ panic_message = message; longjmp(terminal,1); }

// ====== reset_private ======

static void reset_private(void)
{
    memset(&memory,0,sizeof(memory)); memset(arena_bytes,0xcc,sizeof(arena_bytes));
    memory.Context.MemoryMap = arena_bytes; memory.Context.Info.PageTableRoot = 0x1000;
    memory.Context.Info.MemoryMapBytes = 48; memory.Context.Info.MemoryDescriptorBytes = 48;
    memory.Inventory.ConventionalPages = 1024;
    calls = physical_claims = allocations = 0; panic_message = NULL;
}

// ====== main ======

int main(void)
{
    unsigned int total_steps;
    reset_private();
    if (setjmp(terminal)) check_private(0);
    kernel_bootstrap_memory_prepare(&memory);
    total_steps = calls;
    check_private(physical_claims == 1 && allocations == 2 && memory.Physical.FreePages == 1008);
    check_private(!memory.Handles[0].Pages && memory.Arena.Objects.Count == 2);
    check_private(!memcmp(arena_bytes + memory.ContextObject.Offset,&memory.Context,sizeof(memory.Context)));
    check_private(!memcmp(arena_bytes + memory.InventoryObject.Offset,&memory.Inventory,sizeof(memory.Inventory)));
    check_private(arena_bytes[65535] == 0xcc); // Bootstrap does not scan/write an unrelated page.
    for (fail_call = 1; fail_call <= total_steps; ++fail_call) {
        reset_private();
        if (!setjmp(terminal)) {
            kernel_bootstrap_memory_prepare(&memory);
            check_private(0);
        }
        check_private(panic_message && calls == fail_call);
        check_private(physical_claims <= 1 && allocations <= 2);
    }
    fail_call = 0; reset_private(); memory.Window.Ready = 1;
    if (!setjmp(terminal)) { kernel_bootstrap_memory_prepare(&memory); check_private(0); }
    check_private(calls == 0 && panic_message != NULL);
    if (!setjmp(terminal)) { kernel_bootstrap_memory_prepare(NULL); check_private(0); }
    check_private(calls == 0 && panic_message != NULL);
    printf("PASS: %u ordinary bootstrap checks; %u injected API failures; mapping/allocator MOCK\n",checks,total_steps);
    return 0;
}
