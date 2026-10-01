#include "file_protocol.h"

static EFI_GUID loaded_image_guid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
static EFI_GUID filesystem_guid = {
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
static CHAR16 check_path[] = L"\\EFI\\BOOT\\boot-check.txt";

// ====== lettura limitata e verifica contenuto ====== //

static EFI_STATUS verify_contents(EFI_FILE_PREFIX *file)
{
    static const UINT8 expected[] = "OS boot check\r\n";
    UINT8 buffer[64];
    UINTN used = 0;
    UINTN count;
    UINTN i;
    EFI_STATUS status;

    if (!file->Read)
        return EFI_UNSUPPORTED;
    while (used < sizeof(buffer)) {
        count = sizeof(buffer) - used;
        status = file->Read(file, &count, buffer + used);
        if (status != EFI_SUCCESS)
            return status;
        if (count > sizeof(buffer) - used)
            return EFI_DEVICE_ERROR;
        if (count == 0)
            break;
        used += count;
    }
    if (used != sizeof(expected) - 1)
        return EFI_LOAD_ERROR;
    for (i = 0; i < used; ++i)
        if (buffer[i] != expected[i])
            return EFI_LOAD_ERROR;
    return EFI_SUCCESS;
}

// ====== apertura volume di provenienza ====== //

EFI_STATUS boot_open_volume(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, EFI_FILE_PREFIX **root)
{
    EFI_BOOT_SERVICES_PREFIX *services;
    EFI_LOADED_IMAGE_PREFIX *loaded;
    EFI_SIMPLE_FILE_SYSTEM *filesystem;
    void *interface = 0;
    EFI_STATUS status;

    if (!root)
        return EFI_INVALID_PARAMETER;
    *root = 0;
    if (!image || !table)
        return EFI_INVALID_PARAMETER;
    services = (EFI_BOOT_SERVICES_PREFIX *)table->BootServices;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->HandleProtocol)
        return EFI_UNSUPPORTED;
    status = services->HandleProtocol(image, &loaded_image_guid, &interface);
    if (status != EFI_SUCCESS)
        return status;
    loaded = (EFI_LOADED_IMAGE_PREFIX *)interface;
    if (!loaded || !loaded->DeviceHandle)
        return EFI_UNSUPPORTED;
    interface = 0;
    status = services->HandleProtocol(loaded->DeviceHandle, &filesystem_guid, &interface);
    if (status != EFI_SUCCESS)
        return status;
    filesystem = (EFI_SIMPLE_FILE_SYSTEM *)interface;
    if (!filesystem || !filesystem->OpenVolume)
        return EFI_UNSUPPORTED;
    status = filesystem->OpenVolume(filesystem, root);
    if (status != EFI_SUCCESS)
        return status;
    if (!*root || !(*root)->Close)
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

// ====== verifica file di prova e chiusura volume ====== //

EFI_STATUS boot_check_file(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    EFI_FILE_PREFIX *root = 0;
    EFI_FILE_PREFIX *file = 0;
    EFI_STATUS status = boot_open_volume(image, table, &root);
    EFI_STATUS close_status;
    if (status != EFI_SUCCESS)
        return status;
    if (!root->Open) {
        status = EFI_UNSUPPORTED;
    } else {
        status = root->Open(root, &file, check_path, 1, 0);
        if (status == EFI_SUCCESS) {
            if (!file || !file->Close) {
                status = EFI_UNSUPPORTED;
            } else {
                status = verify_contents(file);
                close_status = file->Close(file);
                if (status == EFI_SUCCESS)
                    status = close_status;
            }
        }
    }
    close_status = root->Close(root);
    if (status == EFI_SUCCESS)
        status = close_status;
    return status;
}
