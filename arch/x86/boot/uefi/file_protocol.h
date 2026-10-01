#ifndef OS_FILE_PROTOCOL_H
#define OS_FILE_PROTOCOL_H
#include "uefi.h"

// ====== identificatori protocolli ====== //

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(UINT32, UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(UINT32, UINT32, UINTN, UINT64 *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(UINT64, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, void *, UINTN *, UINTN *, UINT32 *);

// ====== prefisso Boot Services ====== //

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
} EFI_BOOT_SERVICES_PREFIX;

// ====== prefisso Loaded Image ====== //

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
} EFI_LOADED_IMAGE_PREFIX;

// ====== prefisso protocollo file ====== //

typedef struct EFI_FILE_PREFIX EFI_FILE_PREFIX;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PREFIX *, EFI_FILE_PREFIX **, CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PREFIX *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PREFIX *, UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PREFIX *, EFI_GUID *, UINTN *, void *);
struct EFI_FILE_PREFIX {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ Read;
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_FILE_GET_INFO GetInfo;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM EFI_SIMPLE_FILE_SYSTEM;
typedef EFI_STATUS (EFIAPI *EFI_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM *, EFI_FILE_PREFIX **);
struct EFI_SIMPLE_FILE_SYSTEM {
    UINT64 Revision;
    EFI_OPEN_VOLUME OpenVolume;
};

// ====== verifica file di avvio ====== //

EFI_STATUS boot_check_file(EFI_HANDLE image, EFI_SYSTEM_TABLE *table);
EFI_STATUS boot_open_volume(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, EFI_FILE_PREFIX **root);
int run_file_tests(void);
#endif
