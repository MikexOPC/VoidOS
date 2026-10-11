use super::*;

// ====== supported_geometry_and_stride ======

#[test]
fn supported_geometry_and_stride() {
    assert_eq!(fb_mode_bytes(1024,768,1024,0),3145728);
    assert_eq!(fb_mode_bytes(1024,768,1088,1),3342336);
    assert_eq!(fb_mode_score(1024,768,1088,1),0);
    assert_eq!(fb_mode_score(800,600,800,0),392);
    assert_eq!(fb_mode_score(1280,720,1280,0),304);
}

// ====== invalid_geometry ======

#[test]
fn invalid_geometry() {
    for values in [(0,768,1024,0),(1921,768,1921,0),(1024,0,1024,0),
        (1024,1081,1024,0),(1024,768,1023,0),(1024,768,4097,0),
        (1024,768,1024,2),(1024,768,1024,3),(u32::MAX,u32::MAX,u32::MAX,0)] {
        assert_eq!(fb_mode_bytes(values.0,values.1,values.2,values.3),0);
        assert_eq!(fb_mode_score(values.0,values.1,values.2,values.3),u32::MAX);
    }
}

// ====== physical_bounds_and_capacity ======

#[test]
fn physical_bounds_and_capacity() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        assert_eq!(fb_descriptor_bytes(limit-3145728,3145728,1024,768,1024,0,bits),3145728);
        assert_eq!(fb_descriptor_bytes(limit-3145724,3145728,1024,768,1024,0,bits),0);
        assert_eq!(fb_descriptor_bytes(limit,3145728,1024,768,1024,0,bits),0);
    }
    for (base,capacity,bits) in [(0,3145728,32),(3,3145728,32),(4096,3145727,32),
        (4096,33554433,32),(u64::MAX,3145728,52),(4096,3145728,31),(4096,3145728,53)] {
        assert_eq!(fb_descriptor_bytes(base,capacity,1024,768,1024,0,bits),0);
    }
}

// ====== exhaustive_height_stride_boundaries ======

#[test]
fn exhaustive_height_stride_boundaries() {
    for height in 1..=1080 {
        for stride in [1,640,1024,1920,4096] {
            let used = fb_mode_bytes(stride.min(1920),height,stride,1);
            assert_eq!(used,u64::from(height)*u64::from(stride)*4);
            assert_eq!(fb_descriptor_bytes(4096,used,stride.min(1920),height,stride,1,32),used);
        }
    }
}
