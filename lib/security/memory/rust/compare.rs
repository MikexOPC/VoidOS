use core::hint::black_box;

// ====== equal ======

pub fn equal(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() {
        return false;
    }
    let mut difference = 0u8;
    for (first, second) in left.iter().zip(right) {
        difference = black_box(difference | (first ^ second));
    }
    black_box(difference) == 0
}
