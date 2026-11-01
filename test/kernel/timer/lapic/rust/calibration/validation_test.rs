#[path = "../../../../../../arch/x86/kernel/timer/lapic/rust/calibration/validation.rs"]
mod validation;

// ====== diagnostic_irq_deadline_bounds ======

#[test]
fn diagnostic_irq_deadline_bounds() {
    let minimum = 35_795 / 2;
    let maximum = 35_795 * 20;
    for ticks in [minimum, minimum + 1, 35_795, maximum] {
        assert_eq!(validation::x86_timer_deadline_valid(ticks), 1);
    }
    for ticks in [0, minimum - 1, maximum + 1, u32::MAX] {
        assert_eq!(validation::x86_timer_deadline_valid(ticks), 0);
    }
}

// ====== bracket_quality_and_order ======

#[test]
fn bracket_quality_and_order() {
    use validation::x86_timer_sample_elapsed as elapsed;
    for mask in [0xffffff, u32::MAX] {
        assert_eq!(elapsed(100,100,36100,36100,mask),36000);
        assert_eq!(elapsed(100,110,36090,36100,mask),35990);
        let base = mask - 100;
        assert_eq!(elapsed(base,base.wrapping_add(10) & mask,
            base.wrapping_add(35990) & mask,base.wrapping_add(36000) & mask,mask),35990);
        for (a,b,c,d) in [(100,99,36000,36100),(100,200,150,36100),
            (100,200,36200,36100),(100,100,35894,35894),
            (100,100,143281,143281),(100,5000,36100,36100),
            (100,100,20000,36100),(100,100,100,100)] {
            assert_eq!(elapsed(a,b,c,d,mask),0);
        }
        assert_eq!(elapsed(0,2250,36000,36000,mask),34875);
        assert_eq!(elapsed(0,2251,36000,36000,mask),0);
    }
    assert_eq!(elapsed(0,0,36000,36000,0xffff),0);
    assert_eq!(elapsed(0x1000000,0,36000,36000,0xffffff),0);
}

// ====== consensus_bounds ======

#[test]
fn consensus_bounds() {
    use validation::x86_timer_sample_consensus as consensus;
    for low in [1,4,1000,u32::MAX / 2,u32::MAX - u32::MAX / 4] {
        let high = low + low / 4;
        assert_eq!(consensus(low,high),low);
        assert_eq!(consensus(high,low),low);
        if high < u32::MAX { assert_eq!(consensus(low,high+1),0); }
    }
    assert_eq!(consensus(0,1),0);
    assert_eq!(consensus(u32::MAX,u32::MAX),u32::MAX);
}

// ====== ciclo senza limite di tick con arresto esplicito oppure saturazione ===== //

#[test]
fn loop_action() {
    for count in [0,3,8,1000,u32::MAX-1,u32::MAX] {
        assert_eq!(validation::x86_timer_loop_action(0,count,count),if count==u32::MAX {2} else {1});
        assert_eq!(validation::x86_timer_loop_action(1,count,count),2);
        assert_eq!(validation::x86_timer_loop_action(2,count,count),0);
        if count > 0 { assert_eq!(validation::x86_timer_loop_action(0,count,count-1),0); }
    }
}

// ====== un solo evento non consumato e nessun wrap delle sequenze ===== //

#[test]
fn event_states() {
    for count in [0,1,3,1024,u32::MAX] {
        assert_eq!(validation::x86_timer_event_fields(count,count,1,0),1);
        assert_eq!(validation::x86_timer_event_fields(count,count,4,0),1);
        assert_eq!(validation::x86_timer_event_fields(count,count,2,1),u32::from(count < u32::MAX));
        if count > 0 {
            assert_eq!(validation::x86_timer_event_fields(count,count-1,3,1),1);
            assert_eq!(validation::x86_timer_event_fields(count,count-1,1,0),0);
        }
    }
    for (count,consumed,phase,active) in [(0,0,0,0),(1,0,0,0),(2,0,3,1),
        (0,1,1,0),(0,0,5,0),(0,0,1,1),(1,0,3,0)] {
        assert_eq!(validation::x86_timer_event_fields(count,consumed,phase,active),
            u32::from((count,consumed,phase,active)==(0,0,0,0)));
    }
}

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
