#include "kernel/security/random.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include "kernel/diagnostics/log.h"

// Intel SDM Vol. 3A §§4.5, 4.6: P=bit 0, R/W=bit 1, U/S=bit 2, PS=bit 7, XD/NX=bit 63.
#define AUDIT_PTE_PRESENT  (1ULL << 0)
#define AUDIT_PTE_WRITE    (1ULL << 1)
#define AUDIT_PTE_USER     (1ULL << 2)
#define AUDIT_PTE_PS       (1ULL << 7)
#define AUDIT_PTE_NX       (1ULL << 63)

// ====== audit_pte_kernel_wx_valid ======

static int audit_pte_kernel_wx_valid(uint64_t pte)
{
    if ((pte & AUDIT_PTE_PRESENT) == 0ULL) {
        return 1;
    }
    if ((pte & AUDIT_PTE_USER) != 0ULL) {
        return 0;
    }
    // Strict W^X: a present kernel page must never be both writable and executable.
    if ((pte & AUDIT_PTE_WRITE) != 0ULL && (pte & AUDIT_PTE_NX) == 0ULL) {
        return 0;
    }
    return 1;
}

// ====== audit_kernel_image_wx ======

static int audit_kernel_image_wx(uint32_t *rx_out, uint32_t *ro_out, uint32_t *rw_out)
{
    // Intel SDM Vol. 3A §4.5: recursive PML4 slot 510 maps paging structures directly.
    const volatile uint64_t *pml4 =
        (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, 510U, 510U, 510U);
    if ((pml4[511U] & AUDIT_PTE_PRESENT) == 0ULL) {
        return 0;
    }
    const volatile uint64_t *pdpt =
        (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, 510U, 510U, 511U);
    if ((pdpt[510U] & AUDIT_PTE_PRESENT) == 0ULL || (pdpt[510U] & AUDIT_PTE_PS) != 0ULL) {
        return 0;
    }
    const volatile uint64_t *pd =
        (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, 510U, 511U, 510U);

    uint32_t rx_pages = 0U;
    uint32_t ro_pages = 0U;
    uint32_t rw_pages = 0U;
    // X86_KERNEL_IMAGE_BYTES is 4 MiB = 2 page directories (each 2 MiB = 512 * 4 KiB).
    for (uint32_t pd_idx = 0U; pd_idx < 2U; ++pd_idx) {
        uint64_t pde = pd[pd_idx];
        if ((pde & AUDIT_PTE_PRESENT) == 0ULL) {
            continue;
        }
        if ((pde & AUDIT_PTE_PS) != 0ULL) {
            return 0;
        }
        const volatile uint64_t *pt =
            (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, 511U, 510U, pd_idx);
        for (uint32_t pt_idx = 0U; pt_idx < 512U; ++pt_idx) {
            uint64_t pte = pt[pt_idx];
            if ((pte & AUDIT_PTE_PRESENT) == 0ULL) {
                continue;
            }
            if (!audit_pte_kernel_wx_valid(pte)) {
                return 0;
            }
            if ((pte & AUDIT_PTE_WRITE) != 0ULL) {
                ++rw_pages;
            } else if ((pte & AUDIT_PTE_NX) == 0ULL) {
                ++rx_pages;
            } else {
                ++ro_pages;
            }
        }
    }
    if (rx_pages == 0U || rw_pages == 0U) {
        return 0;
    }
    *rx_out = rx_pages;
    *ro_out = ro_pages;
    *rw_out = rw_pages;
    return 1;
}

// ====== audit_stack_guards ======

static int audit_stack_guards(int check_syscall_stack)
{
    const volatile uint64_t *pml4 =
        (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, 510U, 510U, 510U);
    if ((pml4[X86_LAYOUT_WINDOWS_SLOT] & AUDIT_PTE_PRESENT) == 0ULL) {
        return 0;
    }
    const volatile uint64_t *pt =
        (const volatile uint64_t *)X86_LAYOUT_RECURSIVE_VIEW(510U, X86_LAYOUT_WINDOWS_SLOT, 0U, 0U);

    // Main kernel stack: lower guard at MAIN_LEAF, 16 RW+NX stack pages, upper guard at MAIN_LEAF+17.
    if ((pt[X86_KERNEL_MAIN_LEAF] & AUDIT_PTE_PRESENT) != 0ULL ||
        (pt[X86_KERNEL_MAIN_LEAF + 17U] & AUDIT_PTE_PRESENT) != 0ULL) {
        return 0;
    }
    for (uint32_t i = 1U; i <= 16U; ++i) {
        uint64_t pte = pt[X86_KERNEL_MAIN_LEAF + i];
        if ((pte & (AUDIT_PTE_PRESENT | AUDIT_PTE_WRITE | AUDIT_PTE_NX)) !=
                (AUDIT_PTE_PRESENT | AUDIT_PTE_WRITE | AUDIT_PTE_NX) ||
            (pte & AUDIT_PTE_USER) != 0ULL) {
            return 0;
        }
    }

    // IST emergency stack lower guard at IST_LEAF.
    if ((pt[X86_KERNEL_IST_LEAF] & AUDIT_PTE_PRESENT) != 0ULL) {
        return 0;
    }

    if (check_syscall_stack) {
        // Syscall/RSP0 stack: lower guard at SYSCALL_LEAF, 4 RW+NX pages, upper guard at SYSCALL_LEAF+5.
        if ((pt[X86_KERNEL_SYSCALL_LEAF] & AUDIT_PTE_PRESENT) != 0ULL ||
            (pt[X86_KERNEL_SYSCALL_LEAF + 5U] & AUDIT_PTE_PRESENT) != 0ULL) {
            return 0;
        }
        for (uint32_t i = 1U; i <= 4U; ++i) {
            uint64_t pte = pt[X86_KERNEL_SYSCALL_LEAF + i];
            if ((pte & (AUDIT_PTE_PRESENT | AUDIT_PTE_WRITE | AUDIT_PTE_NX)) !=
                    (AUDIT_PTE_PRESENT | AUDIT_PTE_WRITE | AUDIT_PTE_NX) ||
                (pte & AUDIT_PTE_USER) != 0ULL) {
                return 0;
            }
        }
    }
    return 1;
}

// ====== kernel_security_audit_wx_and_guards ======

int kernel_security_audit_wx_and_guards(int check_syscall_stack)
{
    uint32_t rx = 0U, ro = 0U, rw = 0U;
    if (!audit_kernel_image_wx(&rx, &ro, &rw)) {
        kernel_panic("KERNEL_WX_VIOLATION");
    }
    if (!audit_stack_guards(check_syscall_stack)) {
        kernel_panic("KERNEL_GUARD_PAGE_INVALID");
    }
    return kernel_text("KERNEL_WX_RX_PAGES: ") && kernel_number(rx) &&
        kernel_text("\nKERNEL_WX_RO_PAGES: ") && kernel_number(ro) &&
        kernel_text("\nKERNEL_WX_RW_PAGES: ") && kernel_number(rw) &&
        kernel_text("\n") &&
        kernel_log(0, "KERNEL_WX_AUDIT_OK") &&
        kernel_log(0, "KERNEL_GUARD_AUDIT_OK");
}
