#ifndef OS_BOOT_START_INFO_H
#define OS_BOOT_START_INFO_H

// ====== contratto numerico di ingresso x64 ====== //

typedef unsigned long long BOOT_U64;
typedef unsigned int BOOT_U32;
#define BOOT_START_SIGNATURE 0x314f464e49545342ULL
#define BOOT_START_VERSION 1U
#define BOOT_START_NXE 1ULL
#define BOOT_START_WP 2ULL
#define BOOT_START_FEATURES (BOOT_START_NXE | BOOT_START_WP)
#define BOOT_PROBE_SUCCESS 0x31454e4f444f534fULL

typedef struct {
    BOOT_U64 Signature;
    BOOT_U32 Version;
    BOOT_U32 Size;
    BOOT_U64 RequiredFeatures;
    BOOT_U64 EnabledFeatures;
    BOOT_U64 PageTableRoot;
    BOOT_U64 StackBase;
    BOOT_U64 StackBytes;
    BOOT_U64 KernelEntry;
    BOOT_U64 Reserved;
} BOOT_START_INFO;

_Static_assert(sizeof(BOOT_START_INFO) == 72, "Contratto ingresso x64");
#endif
