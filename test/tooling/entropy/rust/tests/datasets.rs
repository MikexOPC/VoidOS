// ====== generatore deterministico ======

pub struct Generator(u64);

impl Generator {
    pub fn new(seed: u64) -> Self {
        Self(seed)
    }

    pub fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
}

// ====== dataset per il confronto con lo strumento NIST ======

pub const SAMPLES: usize = 100_000;

pub struct Dataset {
    pub name    : &'static str,
    pub bits    : u32,
    pub symbols : Vec<u8>,
}

fn uniform(seed: u64, bits: u32) -> Vec<u8> {
    let mut generator = Generator::new(seed);
    (0..SAMPLES).map(|_| (generator.next() >> 40) as u8 & ((1u16 << bits) - 1) as u8).collect()
}

fn biased(seed: u64) -> Vec<u8> {
    let mut generator = Generator::new(seed);
    (0..SAMPLES)
        .map(|_| {
            let draw = generator.unit();
            if draw < 0.4 { 0 } else if draw < 0.7 { 1 } else { (generator.next() >> 60) as u8 }
        })
        .collect()
}

fn sticky_bits(seed: u64) -> Vec<u8> {
    let mut generator = Generator::new(seed);
    let mut bit = 0u8;
    (0..SAMPLES)
        .map(|_| {
            if generator.unit() < 0.3 {
                bit ^= 1;
            }
            bit
        })
        .collect()
}

fn noisy_period(seed: u64) -> Vec<u8> {
    let mut generator = Generator::new(seed);
    (0..SAMPLES)
        .map(|index| {
            let base = (index % 23) as u8 * 7;
            if generator.unit() < 0.2 { (generator.next() >> 56) as u8 } else { base }
        })
        .collect()
}

fn drifting(seed: u64) -> Vec<u8> {
    let mut generator = Generator::new(seed);
    let mut level = 3i32;
    (0..SAMPLES)
        .map(|_| {
            level = (level + (generator.next() % 3) as i32 - 1).clamp(0, 7);
            level as u8
        })
        .collect()
}

pub fn all() -> Vec<Dataset> {
    vec![
        Dataset { name: "uniform8", bits: 8, symbols: uniform(0x0001_2026_1107_0001, 8) },
        Dataset { name: "uniform1", bits: 1, symbols: uniform(0x0002_2026_1107_0002, 1) },
        Dataset { name: "uniform2", bits: 2, symbols: uniform(0x0003_2026_1107_0003, 2) },
        Dataset { name: "biased4", bits: 4, symbols: biased(0x0004_2026_1107_0004) },
        Dataset { name: "sticky1", bits: 1, symbols: sticky_bits(0x0005_2026_1107_0005) },
        Dataset { name: "period8", bits: 8, symbols: noisy_period(0x0006_2026_1107_0006) },
        Dataset { name: "drift3", bits: 3, symbols: drifting(0x0007_2026_1107_0007) },
    ]
}
