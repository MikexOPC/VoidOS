#include "../gop.h"
#include "../../console/log.h"
static BOOT_GOP_OWNER owner;
static volatile UINT32 waiting;

// ====== framebuffer_probe_entry ======

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    EFI_STATUS status;
    (void)image;
    if (!table || !table->ConOut) return EFI_INVALID_PARAMETER;
    if (boot_section(table->ConOut,L"selezione video GOP") != EFI_SUCCESS) return EFI_DEVICE_ERROR;
    /* 52 is the x64 architectural address-format ceiling only. This probe
       captures metadata: no CPU mapping, framebuffer store or kernel entry. */
    status = boot_gop_choose(table,52,&owner);
    if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) {
        if (boot_log(table->ConOut,BOOT_WARN,L"GOP_UNAVAILABLE") != EFI_SUCCESS) return EFI_DEVICE_ERROR;
    } else if (status != EFI_SUCCESS || !owner.Valid || owner.Pending || owner.Dirty ||
        owner.Queries != owner.Released) {
        (void)boot_log(table->ConOut,BOOT_FAIL,L"GOP_PROBE_FAILED");
        (void)boot_log_hex(table->ConOut,L"GOP_STATUS: ",status);
    } else {
        OS_FRAMEBUFFER *fb = &owner.Framebuffer;
        UINT64 last = ((UINT64)(fb->Height - 1) * fb->Stride + fb->Width - 1) * 4;
        if (fb_pixel_offset(fb->Width,fb->Height,fb->Stride,fb->Format,fb->Used,0,0) != 0 ||
            fb_pixel_offset(fb->Width,fb->Height,fb->Stride,fb->Format,fb->Used,
                fb->Width - 1,fb->Height - 1) != last ||
            fb_pixel_offset(fb->Width,fb->Height,fb->Stride,fb->Format,fb->Used,fb->Width,0) != UINT64_MAX ||
            fb_pixel_offset(fb->Width,fb->Height,fb->Stride,fb->Format,fb->Used - 1,0,0) != UINT64_MAX ||
            fb_color_word(fb->Format,0x12,0x34,0x56) !=
                (fb->Format ? 0x00123456U : 0x00563412U)) {
            (void)boot_log(table->ConOut,BOOT_FAIL,L"GOP_PROBE_FAILED");
            return EFI_DEVICE_ERROR;
        }
        if (boot_log(table->ConOut,BOOT_OK,L"GOP_PIXEL_VALIDATED") != EFI_SUCCESS) return EFI_DEVICE_ERROR;
        if (boot_log_hex(table->ConOut,L"GOP_WIDTH: ",owner.Framebuffer.Width) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_HEIGHT: ",owner.Framebuffer.Height) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_STRIDE: ",owner.Framebuffer.Stride) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_FORMAT: ",owner.Framebuffer.Format) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_BASE: ",owner.Framebuffer.Base) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_CAPACITY: ",owner.Framebuffer.Capacity) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_USED: ",owner.Framebuffer.Used) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_QUERIES: ",owner.Queries) != EFI_SUCCESS ||
            boot_log_hex(table->ConOut,L"GOP_RELEASED: ",owner.Released) != EFI_SUCCESS ||
            boot_log(table->ConOut,BOOT_OK,L"GOP_SELECTION_READY") != EFI_SUCCESS) return EFI_DEVICE_ERROR;
    }
    /* Keep report/pending ownership live; the host owns timeout and VM stop.
       No new privileged instructions or unverified direct video stores. */
    for (;;) ++waiting;
}
