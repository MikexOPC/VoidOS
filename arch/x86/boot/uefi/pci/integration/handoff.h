#ifndef BOOT_PCI_INTEGRATION_HANDOFF_H
#define BOOT_PCI_INTEGRATION_HANDOFF_H
#include "../acquire.h"
#include "../../handoff/protocol/start_info.h"
EFI_STATUS boot_prepare_vgpu(EFI_SYSTEM_TABLE *table, UINT32 bits,
    BOOT_START_INFO *info, BOOT_PCI_OWNER *owner);
#endif
