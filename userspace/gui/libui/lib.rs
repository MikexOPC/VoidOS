#![cfg_attr(not(test), no_std)]

pub mod anim;
pub mod console_font;
pub mod draw;
pub mod proto;
pub mod style;

#[cfg(test)]
mod tests {
    use crate::anim::{Easing, RectAnim, Tween, eval_easing, lerp_color_bgra};
    use crate::draw::{Canvas, DirtyList, Rect, blend_bgra};
    use crate::proto::{GuiMessage, MSG_CREATE_WINDOW, MSG_RESIZE_WINDOW, PROTO_WIRE_BYTES};
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

        // 20 small disjoint rects on a 1280x720 screen must merge cheapest pairs instead of fullscreen.
        let mut many = DirtyList::new();
        let screen = Rect::new(0, 0, 1280, 720);
        for i in 0..20 {
            many.add(Rect::new(i * 20, (i % 4) * 20, 8, 8), screen);
        }
        assert_eq!(many.as_slice().len(), 16);
        assert!(many.total_area() < 10_000);
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

        let rmsg = GuiMessage::resize_window(1, 1278, 616, 42);
        let rwire = rmsg.encode();
        let rdec = GuiMessage::decode(&rwire).expect("valid resize message");
        assert_eq!(rdec.kind, MSG_RESIZE_WINDOW);
        assert_eq!(rdec.win_id, 1);
        assert_eq!(rdec.a, 42);
        assert_eq!(rdec.e, 1278);
        assert_eq!(rdec.f, 616);

        let cmsg = GuiMessage::clipboard_set(1, b"/HELLO.TXT");
        let cdec = GuiMessage::decode(&cmsg.encode()).expect("valid clipboard set");
        assert_eq!(cdec.kind, crate::proto::MSG_CLIPBOARD_SET);
        assert_eq!(cdec.e, 10);
        assert_eq!(&cdec.title[..10], b"/HELLO.TXT");

        let smsg = GuiMessage::input_scroll(1, 120, 240, -3);
        let sdec = GuiMessage::decode(&smsg.encode()).expect("valid scroll");
        assert_eq!(sdec.kind, crate::proto::MSG_INPUT_SCROLL);
        assert_eq!(sdec.c, 120);
        assert_eq!(sdec.d, 240);
        assert_eq!(sdec.e as i32, -3);
    }

    #[test]
    fn canvas_blit_and_alpha_clip() {
        let mut dst = [rgb(0, 0, 0); 16 * 16];
        let mut src = [rgb(0x11, 0x22, 0x33); 8 * 8];
        src[0] = rgb(0xAA, 0xBB, 0xCC);
        let mut canvas = Canvas::new(&mut dst, 16, 16, 16);
        canvas.set_clip(Rect::new(4, 4, 6, 6));
        canvas.blit_rect(2, 2, &src, 8, 8, 8, Rect::new(0, 0, 8, 8));
        // Outside clip remains black, inside clip gets blitted pixels.
        assert_eq!(canvas.pixels[2 * 16 + 2], rgb(0, 0, 0));
        assert_eq!(canvas.pixels[4 * 16 + 4], rgb(0x11, 0x22, 0x33));

        let semi = [rgba(0xFF, 0x00, 0x00, 128); 4 * 4];
        canvas.blit_alpha_rect(4, 4, &semi, 4, 4, 4, Rect::new(0, 0, 4, 4));
        let blended = canvas.pixels[4 * 16 + 4];
        assert!(((blended >> 16) & 0xFF) > 0x70);
    }

    #[test]
    fn canvas_draw_text_aa_lowercase_uppercase_and_clip() {
        let bg = rgb(0x0B, 0x10, 0x16);
        let fg = rgb(0xE6, 0xED, 0xF3);
        let mut fb = [bg; 64 * 32];
        let mut canvas = Canvas::new(&mut fb, 64, 32, 64);

        canvas.draw_text_aa(4, 4, b"Aa ", fg, bg);

        let mut cell_upper = 0usize;
        let mut cell_lower = 0usize;
        let mut has_aa_blend = false;
        let mut space_dirty = 0usize;
        for row in 0..18usize {
            for col in 0..9usize {
                let pu = canvas.pixels[(4 + row) * 64 + (4 + col)];
                let pl = canvas.pixels[(4 + row) * 64 + (4 + 9 + col)];
                let ps = canvas.pixels[(4 + row) * 64 + (4 + 18 + col)];
                if pu != bg {
                    cell_upper += 1;
                    if pu != fg {
                        has_aa_blend = true;
                    }
                }
                if pl != bg {
                    cell_lower += 1;
                    if pl != fg {
                        has_aa_blend = true;
                    }
                }
                if ps != bg {
                    space_dirty += 1;
                }
            }
        }
        assert!(cell_upper > 10);
        assert!(cell_lower > 10);
        assert_ne!(cell_upper, cell_lower);
        assert!(has_aa_blend);
        assert_eq!(space_dirty, 0);

        canvas.set_clip(Rect::new(40, 4, 9, 18));
        canvas.draw_text_aa(31, 4, b"HH", fg, bg);
        assert_eq!(canvas.pixels[10 * 64 + 34], bg);
    }
}
