use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
use slint::platform::{Platform, PointerEventButton, WindowAdapter, WindowEvent};
use slint::{ComponentHandle, LogicalPosition, PhysicalSize, Rgb8Pixel};
use std::cell::Cell;
use std::io::Write;
use std::rc::Rc;
use std::time::Duration;

slint::include_modules!();

struct DesktopPlatform {
    window: Rc<MinimalSoftwareWindow>,
    time: Rc<Cell<u64>>,
}

impl Platform for DesktopPlatform {
    // ====== create_window_adapter ======

    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
        Ok(self.window.clone())
    }

    // ====== duration_since_start ======

    fn duration_since_start(&self) -> Duration {
        Duration::from_millis(self.time.get())
    }
}

// ====== click ======

fn click(window: &MinimalSoftwareWindow, time: &Cell<u64>, x: f32, y: f32) {
    let position = LogicalPosition::new(x, y);
    window.dispatch_event(WindowEvent::PointerMoved { position });
    window.dispatch_event(WindowEvent::PointerPressed {
        position,
        button: PointerEventButton::Left,
    });
    window.dispatch_event(WindowEvent::PointerReleased {
        position,
        button: PointerEventButton::Left,
    });
    time.set(time.get() + 300);
    slint::platform::update_timers_and_animations();
}

// ====== capture ======

fn capture(window: &MinimalSoftwareWindow, pixels: &mut [Rgb8Pixel], time: &Cell<u64>, name: &str) {
    window.draw_if_needed(|renderer| {
        renderer.render(pixels, 1024);
    });
    time.set(time.get() + 300);
    slint::platform::update_timers_and_animations();
    window.draw_if_needed(|renderer| {
        renderer.render(pixels, 1024);
    });
    let directory = std::path::PathBuf::from(std::env::args_os().nth(1).expect("output directory"));
    std::fs::create_dir_all(&directory).unwrap();
    let mut image = std::io::BufWriter::new(std::fs::File::create(directory.join(name)).unwrap());
    image.write_all(b"P6\n1024 768\n255\n").unwrap();
    for pixel in pixels {
        image.write_all(&[pixel.r, pixel.g, pixel.b]).unwrap();
    }
}

// ====== main ======

fn main() {
    let window = MinimalSoftwareWindow::new(RepaintBufferType::ReusedBuffer);
    let time = Rc::new(Cell::new(0));
    window.set_size(PhysicalSize::new(1024, 768));
    slint::platform::set_platform(Box::new(DesktopPlatform {
        window: window.clone(),
        time: time.clone(),
    })).unwrap();
    let ui = VoidDesktop::new().unwrap();
    ui.set_display_label("1024 × 768".into());
    ui.set_uptime_label("42 s".into());
    ui.show().unwrap();
    let mut pixels = vec![Rgb8Pixel::default(); 1024 * 768];
    slint::platform::update_timers_and_animations();
    capture(&window, &mut pixels, &time, "desktop.ppm");
    assert!(!ui.get_terminal_open());
    assert!(!ui.get_sysmon_open());
    let launched = Rc::new(Cell::new(-1));
    let requested = launched.clone();
    ui.on_launch_app(move |kind| requested.set(kind));
    click(&window, &time, 403.0, 710.0);
    assert_eq!(launched.get(), 0, "terminal launch request");
    click(&window, &time, 478.0, 710.0);
    assert_eq!(launched.get(), 1, "monitor launch request");
    launched.set(-1);
    click(&window, &time, 330.0, 710.0);
    capture(&window, &mut pixels, &time, "launcher.ppm");
    click(&window, &time, 315.0, 610.0);
    assert_eq!(launched.get(), 0, "launcher terminal request");
    click(&window, &time, 554.0, 710.0);
    assert_eq!(ui.get_dock_mode(), 1, "dock layout action");
    capture(&window, &mut pixels, &time, "wide-dock.ppm");
    ui.set_dock_mode(2);
    capture(&window, &mut pixels, &time, "compact-dock.ppm");
    ui.set_dock_mode(3);
    capture(&window, &mut pixels, &time, "left-dock.ppm");
    ui.set_dock_mode(0);
    capture(&window, &mut pixels, &time, "floating-dock.ppm");
    let previous = pixels.clone();
    ui.set_uptime_label("43 s".into());
    assert_eq!(ui.get_uptime_label().as_str(), "43 s");
    capture(&window, &mut pixels, &time, "uptime.ppm");
    assert!(pixels.iter().zip(previous).any(|(now, old)| {
        now.r != old.r || now.g != old.g || now.b != old.b
    }), "uptime update must repaint pixels");
    println!("PASS: production desktop rendering, app launch callbacks, dock layouts and uptime; host platform and clock");
}
