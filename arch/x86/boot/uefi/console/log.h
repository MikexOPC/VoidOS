#ifndef OS_BOOT_LOG_H
#define OS_BOOT_LOG_H
#include "uefi.h"

// ====== livelli di diagnostica ====== //

typedef enum { BOOT_OK, BOOT_INFO, BOOT_WARN, BOOT_FAIL } BOOT_LOG_LEVEL;
EFI_STATUS boot_log(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_LOG_LEVEL level, CHAR16 *message);
EFI_STATUS boot_section(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *name);
EFI_STATUS boot_log_hex(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *label, UINT64 value);
#endif
