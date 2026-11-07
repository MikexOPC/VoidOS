#[path = "../../../../../../lib/security/random/drbg/rust/drbg.rs"]
mod drbg;

use drbg::*;

// ====== vettori CAVP ======

const NO_RESEED : &str = include_str!("../vectors/hmac_drbg_no_reseed.rsp");
const PR_FALSE  : &str = include_str!("../vectors/hmac_drbg_pr_false.rsp");

type Vector = Vec<(String, Vec<u8>)>;

fn hex(text: &str) -> Vec<u8> {
    text.as_bytes()
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

fn vectors(file: &str) -> Vec<Vector> {
    let mut all = Vec::new();
    let mut current: Option<Vector> = None;
    for line in file.lines() {
        if line.starts_with('[') {
            continue;
        }
        let empty = || line.strip_suffix(" =").map(|name| (name, ""));
        let Some((name, value)) = line.split_once(" = ").or_else(empty) else {
            continue;
        };
        if name == "COUNT" {
            current = Some(Vec::new());
            continue;
        }
        let vector = current.as_mut().expect("campo fuori da un vettore");
        vector.push((name.to_string(), hex(value.trim())));
        if name == "ReturnedBits" {
            all.push(current.take().unwrap());
        }
    }
    all
}

fn field<'a>(vector: &'a Vector, name: &str, occurrence: usize) -> &'a [u8] {
    &vector.iter().filter(|(key, _)| key == name).nth(occurrence).expect(name).1
}

#[test]
fn cavp_no_reseed_vectors() {
    let all = vectors(NO_RESEED);
    assert_eq!(all.len(), 240);
    for vector in &all {
        let mut drbg = HmacDrbg::instantiate(field(vector, "EntropyInput", 0), field(vector, "Nonce", 0), field(vector, "PersonalizationString", 0)).unwrap();
        let mut output = vec![0u8; field(vector, "ReturnedBits", 0).len()];
        drbg.generate(&mut output, field(vector, "AdditionalInput", 0)).unwrap();
        drbg.generate(&mut output, field(vector, "AdditionalInput", 1)).unwrap();
        assert_eq!(output, field(vector, "ReturnedBits", 0));
        assert_eq!(drbg.reseed_counter(), 3);
    }
}

#[test]
fn cavp_reseed_vectors() {
    let all = vectors(PR_FALSE);
    assert_eq!(all.len(), 240);
    for vector in &all {
        let mut drbg = HmacDrbg::instantiate(field(vector, "EntropyInput", 0), field(vector, "Nonce", 0), field(vector, "PersonalizationString", 0)).unwrap();
        drbg.reseed(field(vector, "EntropyInputReseed", 0), field(vector, "AdditionalInputReseed", 0)).unwrap();
        assert_eq!(drbg.reseed_counter(), 1);
        let mut output = vec![0u8; field(vector, "ReturnedBits", 0).len()];
        drbg.generate(&mut output, field(vector, "AdditionalInput", 0)).unwrap();
        drbg.generate(&mut output, field(vector, "AdditionalInput", 1)).unwrap();
        assert_eq!(output, field(vector, "ReturnedBits", 0));
    }
}

#[test]
fn vector_files_cover_every_input_combination() {
    let all = vectors(PR_FALSE);
    let with = |name: &str, occurrence: usize| all.iter().filter(|vector| !field(vector, name, occurrence).is_empty()).count();
    assert!(with("PersonalizationString", 0) > 0 && with("PersonalizationString", 0) < all.len());
    assert!(with("AdditionalInput", 1) > 0 && with("AdditionalInput", 1) < all.len());
    assert!(with("AdditionalInputReseed", 0) > 0);
    assert!(all.iter().all(|vector| field(vector, "ReturnedBits", 0).len() == 128));
}

// ====== richieste e limiti ======

fn seeded() -> HmacDrbg {
    HmacDrbg::instantiate(&[0x11; 32], &[0x22; 16], b"test").unwrap()
}

#[test]
fn instantiate_rejects_short_entropy_and_nonce() {
    assert_eq!(MINIMUM_ENTROPY_BYTES as u32 * 8, SECURITY_STRENGTH_BITS);
    assert_eq!(MINIMUM_NONCE_BYTES as u32 * 8, SECURITY_STRENGTH_BITS / 2);
    assert_eq!(HmacDrbg::instantiate(&[0; 31], &[0; 16], &[]).err(), Some(DrbgError::EntropyTooShort));
    assert_eq!(HmacDrbg::instantiate(&[0; 32], &[0; 15], &[]).err(), Some(DrbgError::NonceTooShort));
    assert!(HmacDrbg::instantiate(&[0; 32], &[0; 16], &[]).is_ok());
    assert_eq!(seeded().reseed(&[0; 31], &[]), Err(DrbgError::EntropyTooShort));
}

#[test]
fn input_and_request_limits_follow_table_two() {
    assert!(input_length_allowed(0));
    assert!(input_length_allowed(1 << 32));
    assert!(!input_length_allowed((1 << 32) + 1));
    assert!(!input_length_allowed(usize::MAX));
    let mut drbg = seeded();
    let mut largest = vec![0u8; MAXIMUM_REQUEST_BYTES];
    assert_eq!(drbg.generate(&mut largest, &[]), Ok(()));
    let mut too_large = vec![0u8; MAXIMUM_REQUEST_BYTES + 1];
    let counter = drbg.reseed_counter();
    assert_eq!(drbg.generate(&mut too_large, &[]), Err(DrbgError::RequestTooLong));
    assert!(too_large.iter().all(|byte| *byte == 0));
    assert_eq!(drbg.reseed_counter(), counter);
}

#[test]
fn reseed_is_required_after_the_interval() {
    assert_eq!(seeded().with_reseed_interval(0).err(), Some(DrbgError::InvalidReseedInterval));
    assert_eq!(seeded().with_reseed_interval(MAXIMUM_RESEED_INTERVAL + 1).err(), Some(DrbgError::InvalidReseedInterval));
    assert!(seeded().with_reseed_interval(MAXIMUM_RESEED_INTERVAL).is_ok());
    let mut drbg = seeded().with_reseed_interval(3).unwrap();
    let mut output = [0u8; 16];
    for _ in 0..3 {
        assert_eq!(drbg.generate(&mut output, &[]), Ok(()));
    }
    let before = output;
    assert_eq!(drbg.generate(&mut output, &[]), Err(DrbgError::ReseedRequired));
    assert_eq!(output, before);
    drbg.reseed(&[0x33; 32], &[]).unwrap();
    assert_eq!(drbg.generate(&mut output, &[]), Ok(()));
}

#[test]
fn partial_blocks_are_prefixes_of_the_stream() {
    let mut full = [0u8; 100];
    seeded().generate(&mut full, &[]).unwrap();
    for length in [0, 1, 31, 32, 33, 64, 99] {
        let mut part = vec![0u8; length];
        seeded().generate(&mut part, &[]).unwrap();
        assert_eq!(part, full[..length]);
    }
}

#[test]
fn every_input_changes_the_output() {
    let base = {
        let mut output = [0u8; 32];
        seeded().generate(&mut output, &[]).unwrap();
        output
    };
    let outputs = [
        HmacDrbg::instantiate(&[0x12; 32], &[0x22; 16], b"test"),
        HmacDrbg::instantiate(&[0x11; 32], &[0x23; 16], b"test"),
        HmacDrbg::instantiate(&[0x11; 32], &[0x22; 16], b"tesT"),
        HmacDrbg::instantiate(&[0x11; 32], &[0x22; 16], b""),
    ];
    for drbg in outputs {
        let mut output = [0u8; 32];
        drbg.unwrap().generate(&mut output, &[]).unwrap();
        assert_ne!(output, base);
    }
    let mut with_additional = [0u8; 32];
    seeded().generate(&mut with_additional, b"x").unwrap();
    assert_ne!(with_additional, base);
    let mut first = [0u8; 32];
    let mut second = [0u8; 32];
    let mut drbg = seeded();
    drbg.generate(&mut first, &[]).unwrap();
    drbg.generate(&mut second, &[]).unwrap();
    assert_ne!(first, second);
}

// ====== azzeramento alla distruzione ======

#[test]
fn dropped_state_is_wiped() {
    let mut storage = std::mem::MaybeUninit::<HmacDrbg>::uninit();
    let mut drbg = seeded();
    drbg.generate(&mut [0u8; 8], &[]).unwrap();
    storage.write(drbg);
    let bytes = unsafe {
        let pointer = storage.as_mut_ptr();
        std::ptr::drop_in_place(pointer);
        std::slice::from_raw_parts(pointer.cast::<u8>(), std::mem::size_of::<HmacDrbg>()).to_vec()
    };
    assert_eq!(bytes.len(), 80);
    assert!(bytes.iter().all(|byte| *byte == 0));
}
