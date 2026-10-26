# K1: the kernel image window read from arch/x86/configs/layout.h, the only place that defines it.
# Build scripts (link base, PE to ELF conversion) and test runners dot-source this file instead
# of writing the addresses by hand.
$layoutHeader = Get-Content -Raw (Join-Path $PSScriptRoot '../../arch/x86/configs/layout.h')

function Read-LayoutValue([string]$Name) {
    $match = [regex]::Match($layoutHeader, "#define\s+$Name\s+0x([0-9A-Fa-f]+)ULL")
    if (!$match.Success) { throw "layout.h: $Name missing" }
    return [Convert]::ToUInt64($match.Groups[1].Value, 16)
}

function Read-LayoutSlot([string]$Name) {
    $match = [regex]::Match($layoutHeader, "#define\s+$Name\s+(\d+)U")
    if (!$match.Success) { throw "layout.h: $Name missing" }
    return [uint32]$match.Groups[1].Value
}

# Intel SDM Vol. 3A 3.3.7.1: slots 256-511 are the upper half, bits 63:48 copy bit 47.
function Get-LayoutSlotAddress([string]$Name) {
    $slot = [uint64](Read-LayoutSlot $Name)
    $address = $slot -shl 39
    if ($slot -ge 256) { $address = $address -bor 0xFFFF000000000000 }
    return [uint64]$address
}

$OsImageBase = Read-LayoutValue 'X86_KERNEL_IMAGE_BASE'
$OsImageLimit = $OsImageBase + (Read-LayoutValue 'X86_KERNEL_IMAGE_BYTES')
# Base of the windows slot (boot stack, page window, arena, LAPIC, task stacks).
$OsWindows = Get-LayoutSlotAddress 'X86_LAYOUT_WINDOWS_SLOT'

# ====== Assert-LayoutCopies ======

# Rust objects cannot include layout.h: their copies of layout values are compared here at every
# kernel build, so a later K1 stage cannot change one side only.
function Assert-LayoutCopies {
    $windows = Get-LayoutSlotAddress 'X86_LAYOUT_WINDOWS_SLOT'
    $fb = Get-LayoutSlotAddress 'X86_LAYOUT_FB_SLOT'
    $copies = @(
        @('arch/x86/kernel/exceptions/rust/frame/validation.rs', 'KERNEL_IMAGE_BASE', $OsImageBase),
        @('arch/x86/kernel/descriptors/rust/layout/validation.rs', 'WINDOWS', $windows),
        @('arch/x86/mm/regions/rust/layout/validation.rs', 'WINDOWS', $windows),
        @('arch/x86/mm/paging/rust/validation/entries.rs', 'LAPIC_VIRTUAL', ($windows + 0x120000)),
        @('arch/x86/mm/paging/rust/validation/entries.rs', 'VIDEO', ($fb + 4096))
    )
    foreach ($copy in @(,@('arch/x86/mm/paging/rust/validation/entries.rs', 'RECURSIVE', [uint64](Read-LayoutSlot 'X86_LAYOUT_RECURSIVE_SLOT')))) {
        $rust = Get-Content -Raw (Join-Path $PSScriptRoot ('../../' + $copy[0]))
        $match = [regex]::Match($rust, "const $($copy[1]): u64 = (\d+);")
        if (!$match.Success -or [uint64]$match.Groups[1].Value -ne $copy[2]) { throw "$($copy[0]): $($copy[1]) differs from layout.h" }
    }
    foreach ($copy in $copies) {
        $rust = Get-Content -Raw (Join-Path $PSScriptRoot ('../../' + $copy[0]))
        $match = [regex]::Match($rust, "const $($copy[1]): u64 = 0x([0-9A-Fa-f_]+);")
        if (!$match.Success -or [Convert]::ToUInt64($match.Groups[1].Value.Replace('_', ''), 16) -ne $copy[2]) {
            throw "$($copy[0]): $($copy[1]) differs from layout.h"
        }
    }
}
