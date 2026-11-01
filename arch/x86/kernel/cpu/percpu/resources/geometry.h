#ifndef OS_X86_AP_GEOMETRY_H
#define OS_X86_AP_GEOMETRY_H
#include <stdint.h>
#include "arch/x86/configs/layout.h"
#define X86_AP_SLOT X86_LAYOUT_AP_SLOT
#define X86_AP_BASE (X86_LAYOUT_SLOT_ADDRESS(X86_AP_SLOT) + 0x200000ULL)
#define X86_AP_STRIDE 0x40000ULL
#define X86_AP_TAG 60U
#define X86_AP_NX 0x8000000000000000ULL
#define X86_AP_ADDRESS 0x000ffffffffff000ULL

// ====== x86_ap_tables ======

static inline uint32_t x86_ap_tables(uint32_t cpus) { return cpus > 1 && cpus <= 64 ? 2 + (cpus + 7) / 8 : 0; }

// ====== x86_ap_page ======

static inline uint32_t x86_ap_page(uint32_t cpus, uint64_t page) {
    uint64_t cpu = page / 64, local = page % 64;
    if (!cpu || cpu >= cpus || cpus > 64) return UINT32_MAX;
    uint32_t offset;
    if (local >= 1 && local <= 16) offset = (uint32_t)local - 1;
    else if (local >= 19 && local <= 22) offset = 16 + (uint32_t)local - 19;
    else if (local >= 25 && local <= 28) offset = 20 + (uint32_t)local - 25;
    else if (local >= 31 && local <= 34) offset = 24 + (uint32_t)local - 31;
    else if (local == 48) offset = 28;
    else return UINT32_MAX;
    return ((uint32_t)cpu - 1) * 29 + offset;
}
uint32_t xa_geometry_page(uint32_t cpus, uint64_t page);
uint32_t xa_geometry_tables(uint32_t cpus);
#endif
