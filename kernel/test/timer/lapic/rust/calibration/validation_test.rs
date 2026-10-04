#[path = "../../../../../../arch/x86/kernel/timer/lapic/rust/calibration/validation.rs"]
mod validation;

// ====== risultati attesa finita senza confondere budget e tempo massimo ===== //

#[test]
fn wait_result() {
    for goal in 1..=3 {
        for sleeps in 0..=8 {
            assert_eq!(validation::x86_timer_wait_result(sleeps+1,8,goal,0,goal),1);
        }
        for (result,budget,count,error) in [(0,8,goal,0),(10,8,goal,0),
            (1,0,goal,0),(1,1025,goal,0),(1,8,goal-1,0),(1,8,goal,1)] {
            assert_eq!(validation::x86_timer_wait_result(result,budget,count,error,goal),0);
        }
    }
    assert_eq!(validation::x86_timer_wait_result(1,8,0,0,0),0);
    assert_eq!(validation::x86_timer_wait_result(1,8,4,0,4),0);
}

// ====== snapshot firmware scaduto oppure ancora attivo ===== //

#[test]
fn previous() {
    assert_eq!(validation::x86_timer_previous_valid(0x10040,0xb,0),1);
    for (lvt,divide,current) in [(0x10040,0xb,1),(0x40,3,0),(0x30040,3,0),
        (0x100ff,3,0),(0x1001f,3,0),(0x10040,4,0)] {
        assert_eq!(validation::x86_timer_previous_valid(lvt,divide,current),0);
    }
}

// ====== wrap reale e campioni troppo lunghi oppure non coerenti ===== //

#[test]
fn elapsed() {
    assert_eq!(validation::x86_pm_elapsed(0xfffff0,0x10,0xffffff),32);
    assert_eq!(validation::x86_pm_elapsed(0xfffffff0,0x10,u32::MAX),32);
    for mask in [0,0xffff,0xffffff,u32::MAX] {
        assert_eq!(validation::x86_pm_elapsed(4,8,mask),if matches!(mask,0xffffff | 0xffffffff) {4} else {0});
    }
    assert_eq!(validation::x86_pm_elapsed(20,19,0xffffff),0);
    assert_eq!(validation::x86_pm_elapsed(0x1000000,0,0xffffff),0);
}

// ====== conversione del campione senza frequenze CPU presunte ===== //

#[test]
fn reload() {
    assert_eq!(validation::x86_timer_reload(100001,1,35795),100000);
    assert_eq!(validation::x86_timer_reload(u32::MAX,1,35795),u32::MAX-1);
    assert_eq!(validation::x86_timer_reload(100001,1,71590),50000);
    for (start,end,pm) in [(1,1,35795),(2,0,35795),(1,2,35795),(2,1,35794),(2,1,143181)] {
        assert_eq!(validation::x86_timer_reload(start,end,pm),0);
    }
}

// ====== porte dword e profili hardware ridotti esplicitamente rifiutati ===== //

#[test]
fn ports() {
    for port in 0..=65536 {
        assert_eq!(validation::x86_pm_clock_port(port,0,4),
            u32::from((0x100..=0xfffc).contains(&port) && port & 3 == 0 && !(0x3f8..0x400).contains(&port)));
    }
    assert_eq!(validation::x86_pm_clock_port(0x408,1<<20,4),0);
    assert_eq!(validation::x86_pm_clock_port(0x408,0,3),0);
}
