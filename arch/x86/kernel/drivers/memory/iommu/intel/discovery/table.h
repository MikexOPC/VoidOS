#ifndef X86_DMAR_DISCOVERY_H
#define X86_DMAR_DISCOVERY_H
#include <stddef.h>
#include <stdint.h>
#define X86_DMAR_MAX_BYTES 65536U
#define X86_DMAR_MAX_UNITS 8U
#define X86_DMAR_MAX_SCOPES 32U
typedef struct {
    uint64_t  Base, Bytes;
    uint16_t  Segment;
    uint8_t   Flags, FirstScope;
    uint32_t  ScopeCount;
} X86_DMAR_UNIT;
typedef struct { uint8_t Unit, Type, Enumeration, Bus, Depth, Reserved[3], Path[16]; } X86_DMAR_SCOPE;
typedef struct {
    uint64_t  Physical;
    // Rmrr: reserved memory region reports (type 1, VT-d 5.0 8.4), firmware-owned DMA targets.
    uint32_t  HostBits, Flags, Units, Scopes, OtherRecords, Rmrr;
    X86_DMAR_UNIT   Unit[8];
    X86_DMAR_SCOPE  Scope[32];
} X86_DMAR_INFO;
_Static_assert(sizeof(X86_DMAR_UNIT)==24 && sizeof(X86_DMAR_SCOPE)==24 && sizeof(X86_DMAR_INFO)==992,"DMAR inventory ABI");
uint32_t x86_dmar_header(uint32_t width,uint32_t flags,uint32_t revision);
uint64_t x86_dmar_register_span(uint64_t base,uint32_t size,uint32_t bits);
uint32_t x86_dmar_scope_fields(uint32_t type,uint32_t length,uint32_t flags,uint32_t reserved,uint32_t enumeration);
int x86_dmar_decode(const unsigned char *data,size_t length,uint32_t physical_bits,X86_DMAR_INFO *output);
#endif
