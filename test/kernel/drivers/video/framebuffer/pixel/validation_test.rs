use super::*;

// ====== pixel_edges_and_padding ======

#[test]
fn pixel_edges_and_padding() {
    assert_eq!(pixel_offset(3, 2, 5, 0, 40, 0, 0), 0);
    assert_eq!(pixel_offset(3, 2, 5, 1, 40, 2, 1), 28);
    for x in 3..=5 {
        assert_eq!(pixel_offset(3, 2, 5, 0, 40, x, 0), u64::MAX);
    }
    assert_eq!(pixel_offset(3, 2, 5, 0, 40, 0, 2), u64::MAX);
}

// ====== invalid_pixel_buffers ======

#[test]
fn invalid_pixel_buffers() {
    for bytes in [0, 1, 3, 39] {
        assert_eq!(pixel_offset(3, 2, 5, 0, bytes, 0, 0), u64::MAX);
    }
    for (width, height, stride, format, x, y) in [
        (0, 2, 5, 0, 0, 0), (3, 0, 5, 0, 0, 0), (3, 2, 2, 0, 0, 0),
        (3, 2, 5, 2, 0, 0), (3, 2, 5, 0, u32::MAX, u32::MAX),
        (u32::MAX, u32::MAX, u32::MAX, 0, 0, 0)] {
        assert_eq!(pixel_offset(width, height, stride, format, u64::MAX, x, y), u64::MAX);
    }
}

// ====== exhaustive_small_surfaces ======

#[test]
fn exhaustive_small_surfaces() {
    for width in 1..=16 {
        for height in 1..=16 {
            for padding in 0..=4 {
                let stride = width + padding;
                let bytes = u64::from(stride) * u64::from(height) * 4;
                for y in 0..=height {
                    for x in 0..=stride {
                        let offset = pixel_offset(width, height, stride, 0, bytes, x, y);
                        if x >= width || y >= height {
                            assert_eq!(offset, u64::MAX);
                        } else {
                            assert_eq!(offset, u64::from(y * stride + x) * 4);
                            assert!(offset + 4 <= bytes);
                        }
                    }
                }
            }
        }
    }
}

// ====== color_channel_order ======

#[test]
fn color_channel_order() {
    for value in 0..=255 {
        assert_eq!(color_word(0, value, 0, 0).to_le_bytes(), [value, 0, 0, 0]);
        assert_eq!(color_word(1, value, 0, 0).to_le_bytes(), [0, 0, value, 0]);
        assert_eq!(color_word(0, 0, value, 0).to_le_bytes(), [0, value, 0, 0]);
        assert_eq!(color_word(1, 0, 0, value).to_le_bytes(), [value, 0, 0, 0]);
    }
    assert_eq!(color_word(0, 255, 255, 255), 0x00ffffff);
    assert_eq!(color_word(1, 0x12, 0x34, 0x56), 0x00123456);
    assert_eq!(color_word(2, 1, 2, 3), u32::MAX);
    assert_eq!(color_word(u32::MAX, 1, 2, 3), u32::MAX);
}
