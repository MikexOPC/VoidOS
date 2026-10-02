#include "capsule.h"
#include "lib/memory/address_ranges.h"

// ====== confronto del template assembly con l'oracolo Rust ====== //

static int template_valid(void)
{
    UINT32 byte;
    UINTN begin = (UINTN)x86_boot_capsule_begin;
    UINTN end = (UINTN)x86_boot_capsule_end;
    if (end < begin || end - begin != BOOT_CAPSULE_BYTES) return 0;
    for (byte = 0; byte < BOOT_CAPSULE_BYTES; ++byte)
        if (x86_boot_capsule_begin[byte] != boot_capsule_expected_byte(byte)) return 0;
    return 1;
}

// ====== preparazione in pagina posseduta senza allocazione nascosta ====== //

int boot_prepare_capsule(UINT8 *page, UINTN bytes)
{
    UINT32 byte;
    if (!page || ((UINTN)page & 4095) || bytes != 4096 ||
        os_address_ranges_overlap((UINT64)(UINTN)page, bytes,
                                  (UINT64)(UINTN)x86_boot_capsule_begin, BOOT_CAPSULE_BYTES) ||
        !template_valid()) return 0;
    for (byte = 0; byte < 4096; ++byte)
        page[byte] = byte < BOOT_CAPSULE_BYTES ? x86_boot_capsule_begin[byte] : 0xcc;
    return boot_validate_capsule(page, bytes);
}

// ====== verifica anche del padding prima di pubblicare il codice ====== //

int boot_validate_capsule(const UINT8 *page, UINTN bytes)
{
    UINT32 byte;
    if (!page || ((UINTN)page & 4095) || bytes != 4096) return 0;
    for (byte = 0; byte < 4096; ++byte) {
        UINT32 expected = byte < BOOT_CAPSULE_BYTES ? boot_capsule_expected_byte(byte) : 0xcc;
        if (page[byte] != expected) return 0;
    }
    return 1;
}
