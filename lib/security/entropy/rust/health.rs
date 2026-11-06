#![cfg_attr(not(test), no_std)]

// ====== limiti e tipi ======

pub const STARTUP_SAMPLES                  : u32 = 1024;
pub const BINARY_WINDOW                    : u32 = 1024;
pub const NON_BINARY_WINDOW                : u32 = 512;
pub const MINIMUM_FALSE_POSITIVE_EXPONENT  : u32 = 20;
pub const MAXIMUM_FALSE_POSITIVE_EXPONENT  : u32 = 64;
pub const MAXIMUM_ENTROPY_MILLIBITS        : u32 = 64_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HealthConfig {
    pub min_entropy_millibits   : u32,
    pub false_positive_exponent : u32,
    pub binary                  : bool,
    pub proportion_cutoff       : u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HealthStatus {
    Startup,
    Healthy,
    RepetitionFailure,
    ProportionFailure,
    InvalidSample,
}

#[derive(Clone, Copy, Debug)]
pub struct HealthMonitor {
    binary             : bool,
    repetition_cutoff  : u32,
    proportion_cutoff  : u32,
    window             : u32,
    status             : HealthStatus,
    started            : u32,
    repetition_value   : u64,
    repetition_count   : u32,
    proportion_value   : u64,
    proportion_count   : u32,
    proportion_seen    : u32,
}

// ====== repetition_cutoff ======

pub fn repetition_cutoff(min_entropy_millibits: u32, false_positive_exponent: u32) -> Option<u32> {
    if min_entropy_millibits == 0 || min_entropy_millibits > MAXIMUM_ENTROPY_MILLIBITS {
        return None;
    }
    if !(MINIMUM_FALSE_POSITIVE_EXPONENT..=MAXIMUM_FALSE_POSITIVE_EXPONENT).contains(&false_positive_exponent) {
        return None;
    }
    let scaled = false_positive_exponent.checked_mul(1000)?;
    scaled.div_ceil(min_entropy_millibits).checked_add(1)
}

// ====== window_for ======

pub fn window_for(binary: bool) -> u32 {
    if binary { BINARY_WINDOW } else { NON_BINARY_WINDOW }
}

// ====== HealthMonitor::new ======

impl HealthMonitor {
    pub fn new(config: HealthConfig) -> Option<Self> {
        if config.binary && config.min_entropy_millibits > 1000 {
            return None;
        }
        let repetition = repetition_cutoff(config.min_entropy_millibits, config.false_positive_exponent)?;
        let window = window_for(config.binary);
        if config.proportion_cutoff < 2 || config.proportion_cutoff > window {
            return None;
        }
        Some(Self {
            binary            : config.binary,
            repetition_cutoff : repetition,
            proportion_cutoff : config.proportion_cutoff,
            window,
            status            : HealthStatus::Startup,
            started           : 0,
            repetition_value  : 0,
            repetition_count  : 0,
            proportion_value  : 0,
            proportion_count  : 0,
            proportion_seen   : 0,
        })
    }

// ====== HealthMonitor::feed ======

    pub fn feed(&mut self, sample: u64) -> HealthStatus {
        if self.failed() {
            return self.status;
        }
        if self.binary && sample > 1 {
            return self.fail(HealthStatus::InvalidSample);
        }
        if !self.repetition_passes(sample) {
            return self.fail(HealthStatus::RepetitionFailure);
        }
        if !self.proportion_passes(sample) {
            return self.fail(HealthStatus::ProportionFailure);
        }
        if self.status == HealthStatus::Startup {
            self.started += 1;
            if self.started == STARTUP_SAMPLES {
                self.status = HealthStatus::Healthy;
            }
        }
        self.status
    }

// ====== HealthMonitor::restart ======

    pub fn restart(&mut self) {
        self.status           = HealthStatus::Startup;
        self.started          = 0;
        self.repetition_value = 0;
        self.repetition_count = 0;
        self.proportion_value = 0;
        self.proportion_count = 0;
        self.proportion_seen  = 0;
    }

// ====== HealthMonitor accessori ======

    pub fn status(&self) -> HealthStatus {
        self.status
    }

    pub fn repetition_cutoff(&self) -> u32 {
        self.repetition_cutoff
    }

    pub fn proportion_cutoff(&self) -> u32 {
        self.proportion_cutoff
    }

    pub fn window(&self) -> u32 {
        self.window
    }

    fn failed(&self) -> bool {
        !matches!(self.status, HealthStatus::Startup | HealthStatus::Healthy)
    }

    fn fail(&mut self, status: HealthStatus) -> HealthStatus {
        self.status = status;
        status
    }

// ====== repetition count test ======

    fn repetition_passes(&mut self, sample: u64) -> bool {
        if self.repetition_count != 0 && sample == self.repetition_value {
            self.repetition_count += 1;
            return self.repetition_count < self.repetition_cutoff;
        }
        self.repetition_value = sample;
        self.repetition_count = 1;
        true
    }

// ====== adaptive proportion test ======

    fn proportion_passes(&mut self, sample: u64) -> bool {
        if self.proportion_seen == 0 {
            self.proportion_value = sample;
            self.proportion_count = 1;
            self.proportion_seen  = 1;
            return true;
        }
        if sample == self.proportion_value {
            self.proportion_count += 1;
        }
        self.proportion_seen += 1;
        if self.proportion_seen == self.window {
            self.proportion_seen = 0;
        }
        self.proportion_count < self.proportion_cutoff
    }
}
