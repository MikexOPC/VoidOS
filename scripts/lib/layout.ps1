# K1: the kernel image window read from arch/x86/configs/layout.h, the only place that defines it.
# Build scripts (link base, PE to ELF conversion) and test runners dot-source this file instead
# of writing the addresses by hand.
$layoutHeader = Get-Content -Raw (Join-Path $PSScriptRoot '../../arch/x86/configs/layout.h')

function Read-LayoutValue([string]$Name) {
    $match = [regex]::Match($layoutHeader, "#define\s+$Name\s+0x([0-9A-Fa-f]+)ULL")
    if (!$match.Success) { throw "layout.h: $Name missing" }
    return [Convert]::ToUInt64($match.Groups[1].Value, 16)
}

# Slots below 256 only for now (K1 stages 2-4 add the sign extension of the upper half here).
function Read-LayoutSlot([string]$Name) {
    $match = [regex]::Match($layoutHeader, "#define\s+$Name\s+(\d+)U")
    if (!$match.Success) { throw "layout.h: $Name missing" }
    return [uint32]$match.Groups[1].Value
}

$OsImageBase = Read-LayoutValue 'X86_KERNEL_IMAGE_BASE'
$OsImageLimit = $OsImageBase + (Read-LayoutValue 'X86_KERNEL_IMAGE_BYTES')

# ====== Assert-LayoutCopies ======

# Rust objects cannot include layout.h: their copies of layout values are compared here at every
# kernel build, so a later K1 stage cannot change one side only.
function Assert-LayoutCopies {
    $windows = [uint64](Read-LayoutSlot 'X86_LAYOUT_WINDOWS_SLOT') -shl 39
    $copies = @(
        @('arch/x86/kernel/exceptions/rust/frame/validation.rs', 'KERNEL_IMAGE_BASE', $OsImageBase),
        @('arch/x86/kernel/descriptors/rust/layout/validation.rs', 'GUARD', ($windows + 0x40000)),
        @('arch/x86/mm/paging/rust/validation/entries.rs', 'LAPIC_VIRTUAL', ($windows + 0x120000))
    )
    foreach ($copy in $copies) {
        $rust = Get-Content -Raw (Join-Path $PSScriptRoot ('../../' + $copy[0]))
        $match = [regex]::Match($rust, "const $($copy[1]): u64 = 0x([0-9A-Fa-f_]+);")
        if (!$match.Success -or [Convert]::ToUInt64($match.Groups[1].Value.Replace('_', ''), 16) -ne $copy[2]) {
            throw "$($copy[0]): $($copy[1]) differs from layout.h"
        }
    }
}
