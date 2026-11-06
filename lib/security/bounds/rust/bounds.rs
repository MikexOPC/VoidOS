#![cfg_attr(not(test), no_std)]

// ====== OsBoundsValue ======

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OsBoundsValue {
    pub value  : u64,
    pub valid  : u32,
}

const INVALID: OsBoundsValue = OsBoundsValue { value: 0, valid: 0 };

fn from_option(result: Option<u64>) -> OsBoundsValue {
    match result {
        Some(value) => OsBoundsValue { value, valid: 1 },
        None        => INVALID,
    }
}

fn range_end(base: u64, length: u64) -> Option<u64> {
    base.checked_add(length)
}

// ====== os_bounds_add ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_add(left: u64, right: u64) -> OsBoundsValue {
    from_option(left.checked_add(right))
}

// ====== os_bounds_mul ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_mul(left: u64, right: u64) -> OsBoundsValue {
    from_option(left.checked_mul(right))
}

// ====== os_bounds_align_up ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_align_up(value: u64, alignment: u64) -> OsBoundsValue {
    if !alignment.is_power_of_two() {
        return INVALID;
    }
    let mask = alignment - 1;
    from_option(value.checked_add(mask).map(|total| total & !mask))
}

// ====== os_bounds_align_down ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_align_down(value: u64, alignment: u64) -> OsBoundsValue {
    if !alignment.is_power_of_two() {
        return INVALID;
    }
    from_option(Some(value & !(alignment - 1)))
}

// ====== os_bounds_is_aligned ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_is_aligned(value: u64, alignment: u64) -> u32 {
    u32::from(alignment.is_power_of_two() && value & (alignment - 1) == 0)
}

// ====== os_bounds_range_end ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_range_end(base: u64, length: u64) -> OsBoundsValue {
    from_option(range_end(base, length))
}

// ====== os_bounds_range_within ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_range_within(
    base         : u64,
    length       : u64,
    outer_base   : u64,
    outer_length : u64,
) -> u32 {
    let (Some(end), Some(outer_end)) = (range_end(base, length), range_end(outer_base, outer_length)) else {
        return 0;
    };
    u32::from(base >= outer_base && end <= outer_end)
}

// ====== os_bounds_slice_within ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_slice_within(offset: u64, length: u64, container_length: u64) -> u32 {
    match range_end(offset, length) {
        Some(end) => u32::from(end <= container_length),
        None      => 0,
    }
}

// ====== os_bounds_index_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_index_valid(index: u64, count: u64) -> u32 {
    u32::from(index < count)
}

// ====== os_bounds_array_bytes ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_array_bytes(count: u64, element_size: u64, container_length: u64) -> OsBoundsValue {
    if element_size == 0 {
        return INVALID;
    }
    from_option(count.checked_mul(element_size).filter(|bytes| *bytes <= container_length))
}

// ====== os_bounds_element_offset ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_element_offset(index: u64, count: u64, element_size: u64) -> OsBoundsValue {
    if element_size == 0 || index >= count {
        return INVALID;
    }
    let Some(offset) = index.checked_mul(element_size) else {
        return INVALID;
    };
    from_option(offset.checked_add(element_size).map(|_| offset))
}

// ====== os_bounds_ranges_disjoint ======

#[unsafe(no_mangle)]
pub extern "C" fn os_bounds_ranges_disjoint(
    first_base    : u64,
    first_length  : u64,
    second_base   : u64,
    second_length : u64,
) -> u32 {
    let (Some(first_end), Some(second_end)) = (range_end(first_base, first_length), range_end(second_base, second_length)) else {
        return 0;
    };
    if first_length == 0 || second_length == 0 {
        return 1;
    }
    u32::from(first_end <= second_base || second_end <= first_base)
}
