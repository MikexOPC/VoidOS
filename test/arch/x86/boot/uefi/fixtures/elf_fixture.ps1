# ====== fixture ELF64 sintetica non eseguibile ====== #

function New-KernelElfFixture {
    $bytes = [byte[]]::new(512)
    $bytes[0] = 0x7f
    $bytes[1] = 0x45
    $bytes[2] = 0x4c
    $bytes[3] = 0x46
    $bytes[4] = 2
    $bytes[5] = 1
    $bytes[6] = 1
    foreach ($field in @(
        @{ Offset = 16; Value = [uint16]2 },
        @{ Offset = 18; Value = [uint16]62 },
        @{ Offset = 20; Value = [uint32]1 },
        @{ Offset = 24; Value = [uint64]0x100100 },
        @{ Offset = 32; Value = [uint64]64 },
        @{ Offset = 52; Value = [uint16]64 },
        @{ Offset = 54; Value = [uint16]56 },
        @{ Offset = 56; Value = [uint16]2 },
        @{ Offset = 64; Value = [uint32]1 },
        @{ Offset = 68; Value = [uint32]5 },
        @{ Offset = 72; Value = [uint64]256 },
        @{ Offset = 80; Value = [uint64]0x100100 },
        @{ Offset = 96; Value = [uint64]16 },
        @{ Offset = 104; Value = [uint64]64 },
        @{ Offset = 112; Value = [uint64]256 },
        @{ Offset = 120; Value = [uint32]1 },
        @{ Offset = 124; Value = [uint32]6 },
        @{ Offset = 128; Value = [uint64]384 },
        @{ Offset = 136; Value = [uint64]0x200180 },
        @{ Offset = 152; Value = [uint64]8 },
        @{ Offset = 160; Value = [uint64]64 },
        @{ Offset = 168; Value = [uint64]128 }
    )) {
        [BitConverter]::GetBytes($field.Value).CopyTo($bytes, $field.Offset)
    }
    return ,$bytes
}
