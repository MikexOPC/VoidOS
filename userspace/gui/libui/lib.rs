#![cfg_attr(not(test), no_std)]

pub mod anim;
pub mod draw;
pub mod proto;
pub mod style;

#[cfg(test)]
mod tests {
    use crate::anim::{Easing, RectAnim, Tween, eval_easing, lerp_color_bgra};
    use crate::draw::{Canvas, DirtyList, Rect, blend_bgra};
    use crate::proto::{GuiMessage, MSG_CREATE_WINDOW, PROTO_WIRE_BYTES};
    use crate::style::{DockEdge, DockShape, Theme, rgb, rgba};

    #[test]
    fn easing_and_tween_progression() {
        assert_eq!(eval_easing(Easing::Linear, 0), 0);
        assert_eq!(eval_easing(Easing::Linear, 65_536), 65_536);
        assert!(eval_easing(Easing::EaseOutCubic, 32_768) > 32_768);
        assert!(eval_easing(Easing::EaseOutBack, 49_152) > 65_536);

        let mut tw = Tween::settled(10);
        tw.retarget(100, 110, 200, Easing::Linear);
        assert_eq!(tw.sample(100), 10);
        assert_eq!(tw.sample(200), 60);
        assert_eq!(tw.sample(300), 110);
        assert!(tw.is_done(300));

        let mut ra = RectAnim::settled(0, 0, 100, 40);
        ra.retarget(0, 50, 100, 200, 60, 100, Easing::EaseOutCubic);
        assert!(ra.is_animating(50));
        assert_eq!(ra.sample(100), (50, 100, 200, 60));
        ra.tick(100);
        assert!(!ra.is_animating(100));
    }

    #[test]
    fn mini_css_vss_parser() {
        let vss = b"
            /* VoidOS custom theme */
            desktop { bg-top: #101420; bg-bottom: #202840; }
            dock { edge: left; shape: bar; radius: 10; thickness: 56; transition: 220ms; background: #112233cc; }
            window { background: #12161f; radius: 12; }
            theme { accent: #00d4ff; }
        ";
        let theme = Theme::parse_vss(vss);
        assert_eq!(theme.bg_top, rgb(0x10, 0x14, 0x20));
        assert_eq!(theme.bg_bottom, rgb(0x20, 0x28, 0x40));
        assert_eq!(theme.dock_edge, DockEdge::Left);
        assert_eq!(theme.dock_shape, DockShape::Bar);
        assert_eq!(theme.dock_radius, 10);
        assert_eq!(theme.dock_thickness, 56);
        assert_eq!(theme.anim_ms, 220);
        assert_eq!(theme.dock_bg, rgba(0x11, 0x22, 0x33, 0xCC));
        assert_eq!(theme.window_radius, 12);
        assert_eq!(theme.accent, rgb(0x00, 0xD4, 0xFF));
    }

    #[test]
    fn canvas_fill_clip_blend_and_dirty_list() {
        let mut fb = [0u32; 64 * 32];
        let mut canvas = Canvas::new(&mut fb, 64, 32, 64);
        let bg = rgb(0x10, 0x20, 0x30);
        canvas.fill_rect(Rect::new(0, 0, 64, 32), bg);
        assert_eq!(canvas.pixels[0], bg);
        assert_eq!(canvas.pixels[64 * 32 - 1], bg);

        canvas.set_clip(Rect::new(10, 10, 10, 10));
        let fg = rgb(0xFF, 0x00, 0x00);
        canvas.fill_rect(Rect::new(0, 0, 64, 32), fg);
        assert_eq!(canvas.pixels[0], bg);
        assert_eq!(canvas.pixels[10 * 64 + 10], fg);

        let blended = blend_bgra(rgb(0, 0, 0), rgba(200, 100, 50, 128));
        assert!((blended >> 16 & 0xFF) >= 99 && (blended >> 16 & 0xFF) <= 101);
        assert_eq!(lerp_color_bgra(rgb(0, 0, 0), rgb(100, 200, 50), 32_768), rgb(50, 100, 25));

        let mut dirty = DirtyList::new();
        let bounds = Rect::new(0, 0, 64, 32);
        dirty.add(Rect::new(0, 0, 10, 10), bounds);
        dirty.add(Rect::new(5, 5, 10, 10), bounds);
        assert_eq!(dirty.as_slice().len(), 1);
        assert_eq!(dirty.as_slice()[0], Rect::new(0, 0, 15, 15));
    }

    #[test]
    fn proto_roundtrip_and_magic_validation() {
        let msg = GuiMessage::create_window(640, 480, 7, b"Terminal");
        let wire = msg.encode();
        assert_eq!(wire.len(), PROTO_WIRE_BYTES);
        let decoded = GuiMessage::decode(&wire).expect("valid gui message");
        assert_eq!(decoded.kind, MSG_CREATE_WINDOW);
        assert_eq!(decoded.a, 7);
        assert_eq!(decoded.e, 640);
        assert_eq!(decoded.f, 480);
        assert_eq!(&decoded.title[..8], b"Terminal");

        let mut bad = wire;
        bad[0] ^= 0xFF;
        assert!(GuiMessage::decode(&bad).is_none());
    }
}
