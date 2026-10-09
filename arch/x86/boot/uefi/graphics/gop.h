#ifndef OS_BOOT_GOP_H
#define OS_BOOT_GOP_H
#include "../file_protocol.h"
#include "kernel/drivers/video/framebuffer/framebuffer.h"
#include <stddef.h>

/* UEFI2.10 section12.9.2, EDK2 edk2-stable202508 GraphicsOutput.h.
   ABI declarations independently expressed; enum represented by UINT32. */
typedef struct {
    UINT32 Version, Width, Height, Format;
    UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
    UINT32 Stride;
} BOOT_GOP_INFO;
typedef struct {
    UINT32 MaxMode, Index;
    BOOT_GOP_INFO *Info;
    UINTN SizeOfInfo;
    UINT64 Base;
    UINTN Capacity;
} BOOT_GOP_MODE;
typedef struct BOOT_GOP BOOT_GOP;
typedef EFI_STATUS (EFIAPI *BOOT_GOP_QUERY)(BOOT_GOP *, UINT32, UINTN *, BOOT_GOP_INFO **);
typedef EFI_STATUS (EFIAPI *BOOT_GOP_SET)(BOOT_GOP *, UINT32);
struct BOOT_GOP {
    BOOT_GOP_QUERY Query;
    BOOT_GOP_SET Set;
    void *Blt;
    BOOT_GOP_MODE *Mode;
};
typedef EFI_STATUS (EFIAPI *BOOT_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);
typedef struct {
    EFI_BOOT_SERVICES_PREFIX Prefix;
    /* EDK2 UefiSpec.h EFI_BOOT_SERVICES:20 slots Reserved..LocateHandleBuffer.
       Microsoft x64: prefix160 bytes, LocateProtocol offset320. */
    void *Intermediate[20];
    BOOT_LOCATE_PROTOCOL Locate;
} BOOT_GOP_SERVICES;
_Static_assert(sizeof(BOOT_GOP_INFO) == 36, "GOP mode information ABI");
_Static_assert(sizeof(BOOT_GOP_MODE) == 40, "GOP current mode ABI");
_Static_assert(offsetof(BOOT_GOP_MODE, Base) == 24, "GOP framebuffer base offset");
_Static_assert(sizeof(BOOT_GOP) == 32, "GOP protocol ABI");
_Static_assert(offsetof(BOOT_GOP_SERVICES, Locate) == 320, "LocateProtocol x64 offset");

typedef struct {
    BOOT_GOP_SERVICES *Services;
    BOOT_GOP *Protocol;
    BOOT_GOP_INFO *Pending;
    OS_FRAMEBUFFER Framebuffer;
    UINT32 Original, Selected, Queries, Released;
    UINT32 Attempted, Dirty, Valid, RestoreFailed;
} BOOT_GOP_OWNER;
/* Zero owner, exclusive pre-ExitBootServices lifetime. No framebuffer stores.
   Query buffers are retained in Pending when FreePool fails; retry close.
   Success is metadata capture, not verified CPU mapping or kernel handoff. */
EFI_STATUS boot_gop_choose(EFI_SYSTEM_TABLE *table, UINT32 bits, BOOT_GOP_OWNER *owner);
EFI_STATUS boot_gop_release_pending(BOOT_GOP_OWNER *owner);
EFI_STATUS boot_gop_abandon(BOOT_GOP_OWNER *owner);
#endif
