#ifndef OS_X86_AP_GEOMETRY_H
#define OS_X86_AP_GEOMETRY_H
#include <stdint.h>
#include "arch/x86/configs/layout.h"
#define X86_AP_SLOT X86_LAYOUT_AP_SLOT
#define X86_AP_BASE (X86_LAYOUT_SLOT_ADDRESS(X86_AP_SLOT) + 0x200000ULL)
#define X86_AP_STRIDE 0x40000ULL
#define X86_AP_TAG 64U
#define X86_AP_NX 0x8000000000000000ULL
#define X86_AP_ADDRESS 0x000ffffffffff000ULL

// ====== per_cpu_layout ======

// Software layout of one 64-page AP slot; page 0 and the gaps are unmapped guards.
//   1-16  main stack, private frame in its top 1024 bytes
//   19-22 IST1, 25-28 IST2, 31-34 IST3
//   37-40 syscall / TSS RSP0 stack (4 pages, guarded below and above)
//   48    metadata: GDT at +0, TSS at +64, panic record at +256
#define X86_AP_PAGE                 4096ULL
#define X86_AP_SLOT_PAGES           64ULL
#define X86_AP_REGION_BYTES         (64ULL * X86_AP_STRIDE)
#define X86_AP_CPU_BASE(cpu)        (X86_AP_BASE + (uint64_t)(cpu) * X86_AP_STRIDE)
#define X86_AP_PAGE_ADDRESS(cpu, p) (X86_AP_CPU_BASE(cpu) + (uint64_t)(p) * X86_AP_PAGE)

#define X86_AP_STACK_FIRST          1ULL
#define X86_AP_STACK_PAGES          16ULL
#define X86_AP_STACK_BASE(cpu)      X86_AP_PAGE_ADDRESS(cpu, X86_AP_STACK_FIRST)
#define X86_AP_STACK_TOP(cpu)       X86_AP_PAGE_ADDRESS(cpu, X86_AP_STACK_FIRST + X86_AP_STACK_PAGES)
// Lowest RSP accepted by the runtime checks: one page of headroom above the stack bottom.
#define X86_AP_STACK_FLOOR(cpu)     (X86_AP_STACK_BASE(cpu) + X86_AP_PAGE)

#define X86_AP_FRAME_BYTES          1024ULL
#define X86_AP_FRAME(cpu)           (X86_AP_STACK_TOP(cpu) - X86_AP_FRAME_BYTES)

#define X86_AP_IST_PAGES            4ULL
#define X86_AP_IST_BYTES            (X86_AP_IST_PAGES * X86_AP_PAGE)
#define X86_AP_IST_FIRST(ist)       ((ist) == 1 ? 19ULL : (ist) == 2 ? 25ULL : 31ULL)
#define X86_AP_IST_BASE(cpu, ist)   X86_AP_PAGE_ADDRESS(cpu, X86_AP_IST_FIRST(ist))
#define X86_AP_IST_TOP(cpu, ist)    (X86_AP_IST_BASE(cpu, ist) + X86_AP_IST_BYTES)

#define X86_AP_SYSCALL_FIRST        37ULL
#define X86_AP_SYSCALL_PAGES        4ULL
#define X86_AP_SYSCALL_BASE(cpu)    X86_AP_PAGE_ADDRESS(cpu, X86_AP_SYSCALL_FIRST)
#define X86_AP_SYSCALL_TOP(cpu)     (X86_AP_SYSCALL_BASE(cpu) + X86_AP_SYSCALL_PAGES * X86_AP_PAGE)

#define X86_AP_METADATA_PAGE        48ULL
#define X86_AP_METADATA(cpu)        X86_AP_PAGE_ADDRESS(cpu, X86_AP_METADATA_PAGE)
#define X86_AP_TSS_OFFSET           64ULL
#define X86_AP_RECORD_OFFSET        256ULL
// Mapped pages per AP: stack, three IST stacks, syscall stack and metadata (33, mirrored in rust/geometry/pages.rs).
#define X86_AP_MAPPED_PAGES         (uint32_t)(X86_AP_STACK_PAGES + 3 * X86_AP_IST_PAGES + X86_AP_SYSCALL_PAGES + 1)

_Static_assert(X86_AP_STRIDE == X86_AP_SLOT_PAGES * X86_AP_PAGE, "AP slot is 64 pages");
_Static_assert(X86_AP_STACK_FIRST + X86_AP_STACK_PAGES < X86_AP_IST_FIRST(1), "guard after stack");
_Static_assert(X86_AP_IST_FIRST(1) + X86_AP_IST_PAGES < X86_AP_IST_FIRST(2), "guard after IST1");
_Static_assert(X86_AP_IST_FIRST(2) + X86_AP_IST_PAGES < X86_AP_IST_FIRST(3), "guard after IST2");
_Static_assert(X86_AP_IST_FIRST(3) + X86_AP_IST_PAGES < X86_AP_SYSCALL_FIRST, "guard after IST3");
_Static_assert(X86_AP_SYSCALL_FIRST + X86_AP_SYSCALL_PAGES < X86_AP_METADATA_PAGE, "guard after syscall stack");
_Static_assert(X86_AP_METADATA_PAGE < X86_AP_SLOT_PAGES, "metadata inside the slot");
_Static_assert(X86_AP_MAPPED_PAGES == 33, "matches rust/geometry/pages.rs");

// ====== x86_ap_tables ======

static inline uint32_t x86_ap_tables(uint32_t cpus) { return cpus > 1 && cpus <= 64 ? 2 + (cpus + 7) / 8 : 0; }

// ====== x86_ap_page ======

static inline uint32_t x86_ap_page(uint32_t cpus, uint64_t page) {
    uint64_t cpu = page / X86_AP_SLOT_PAGES, local = page % X86_AP_SLOT_PAGES;
    if (!cpu || cpu >= cpus || cpus > 64) return UINT32_MAX;
    uint64_t offset;
    if (local >= X86_AP_STACK_FIRST && local < X86_AP_STACK_FIRST + X86_AP_STACK_PAGES)
        offset = local - X86_AP_STACK_FIRST;
    else if (local >= X86_AP_IST_FIRST(1) && local < X86_AP_IST_FIRST(1) + X86_AP_IST_PAGES)
        offset = X86_AP_STACK_PAGES + local - X86_AP_IST_FIRST(1);
    else if (local >= X86_AP_IST_FIRST(2) && local < X86_AP_IST_FIRST(2) + X86_AP_IST_PAGES)
        offset = X86_AP_STACK_PAGES + X86_AP_IST_PAGES + local - X86_AP_IST_FIRST(2);
    else if (local >= X86_AP_IST_FIRST(3) && local < X86_AP_IST_FIRST(3) + X86_AP_IST_PAGES)
        offset = X86_AP_STACK_PAGES + 2 * X86_AP_IST_PAGES + local - X86_AP_IST_FIRST(3);
    else if (local >= X86_AP_SYSCALL_FIRST && local < X86_AP_SYSCALL_FIRST + X86_AP_SYSCALL_PAGES)
        offset = X86_AP_STACK_PAGES + 3 * X86_AP_IST_PAGES + local - X86_AP_SYSCALL_FIRST;
    else if (local == X86_AP_METADATA_PAGE)
        offset = X86_AP_STACK_PAGES + 3 * X86_AP_IST_PAGES + X86_AP_SYSCALL_PAGES;
    else return UINT32_MAX;
    return ((uint32_t)cpu - 1) * X86_AP_MAPPED_PAGES + (uint32_t)offset;
}
uint32_t xa_geometry_page(uint32_t cpus, uint64_t page);
uint32_t xa_geometry_tables(uint32_t cpus);
#endif
