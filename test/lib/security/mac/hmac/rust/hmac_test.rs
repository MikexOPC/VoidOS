#[path = "../../../../../../lib/security/mac/hmac/rust/hmac.rs"]
mod hmac;

use hmac::*;

// ====== supporto ======

fn hex(text: &str) -> Vec<u8> {
    let clean: Vec<u8> = text.bytes().filter(|byte| !byte.is_ascii_whitespace()).collect();
    clean
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

fn tag(key: &[u8], data: &[u8]) -> [u8; TAG_BYTES] {
    mac(key, data).expect("lunghezze ammesse")
}

struct Generator(u64);

impl Generator {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    fn bytes(&mut self, length: usize) -> Vec<u8> {
        (0..length).map(|_| self.next() as u8).collect()
    }
}

// ====== oracolo indipendente Windows CNG ======

#[link(name = "bcrypt")]
unsafe extern "system" {
    fn BCryptHash(
        algorithm  : usize,
        secret     : *const u8,
        secret_len : u32,
        input      : *const u8,
        input_len  : u32,
        output     : *mut u8,
        output_len : u32,
    ) -> i32;
}

const BCRYPT_HMAC_SHA256_ALG_HANDLE: usize = 0xb1;

fn cng(key: &[u8], data: &[u8]) -> [u8; TAG_BYTES] {
    let mut output = [0u8; TAG_BYTES];
    let status = unsafe {
        BCryptHash(
            BCRYPT_HMAC_SHA256_ALG_HANDLE,
            key.as_ptr(),
            u32::try_from(key.len()).unwrap(),
            data.as_ptr(),
            u32::try_from(data.len()).unwrap(),
            output.as_mut_ptr(),
            TAG_BYTES as u32,
        )
    };
    assert_eq!(status, 0);
    output
}

// ====== vettori RFC 4231 ======

const LONG_DATA: &[u8] = b"This is a test using a larger than block-size key and a larger than block-size data. \
The key needs to be hashed before being used by the HMAC algorithm.";

fn rfc_4231_cases() -> Vec<(Vec<u8>, Vec<u8>, &'static str)> {
    vec![
        (vec![0x0b; 20], b"Hi There".to_vec(), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
        (b"Jefe".to_vec(), b"what do ya want for nothing?".to_vec(), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
        (vec![0xaa; 20], vec![0xdd; 50], "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"),
        (hex("0102030405060708090a0b0c0d0e0f10111213141516171819"), vec![0xcd; 50], "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"),
        (vec![0x0c; 20], b"Test With Truncation".to_vec(), "a3b6167473100ee06e0c796c2955552b"),
        (vec![0xaa; 131], b"Test Using Larger Than Block-Size Key - Hash Key First".to_vec(), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
        (vec![0xaa; 131], LONG_DATA.to_vec(), "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"),
    ]
}

#[test]
fn rfc_4231_test_cases() {
    assert_eq!(LONG_DATA.len(), 152);
    for (key, data, expected) in rfc_4231_cases() {
        let expected = hex(expected);
        assert_eq!(&tag(&key, &data)[..expected.len()], &expected[..]);
    }
}

#[test]
fn rfc_4231_test_cases_agree_with_cng() {
    for (key, data, _) in rfc_4231_cases() {
        assert_eq!(tag(&key, &data), cng(&key, &data));
    }
}

// ====== chiavi ai confini del blocco ======

#[test]
fn every_key_length_matches_cng() {
    let mut generator = Generator(0x4d41_4320_2026_1107);
    for length in 1..=200 {
        let key = generator.bytes(length);
        let data = generator.bytes(length * 3 % 257);
        assert_eq!(tag(&key, &data), cng(&key, &data), "chiave di {length} byte");
    }
}

#[test]
fn zero_padding_makes_short_keys_equivalent() {
    let data = b"stesso messaggio";
    let empty = tag(&[], data);
    for length in 1..=64 {
        assert_eq!(tag(&vec![0u8; length], data), empty);
    }
    assert_ne!(tag(&[0u8; 65], data), empty);
    let long = vec![0x42u8; 100];
    assert_eq!(tag(&long, data), tag(&cng_digest(&long), data));
}

fn cng_digest(data: &[u8]) -> [u8; 32] {
    let mut output = [0u8; 32];
    let status = unsafe {
        BCryptHash(0x41, std::ptr::null(), 0, data.as_ptr(), u32::try_from(data.len()).unwrap(), output.as_mut_ptr(), 32)
    };
    assert_eq!(status, 0);
    output
}

// ====== aggiornamenti a pezzi e cloni ======

#[test]
fn streaming_and_clones_match_one_shot() {
    let mut generator = Generator(0x0123_4567_89ab_cdef);
    let key = generator.bytes(40);
    let message = generator.bytes(700);
    let expected = tag(&key, &message);
    for split in (0..=message.len()).step_by(7) {
        let mut hmac = HmacSha256::new(&key).unwrap();
        hmac.update(&message[..split]).unwrap();
        let copy = hmac.clone();
        hmac.update(&message[split..]).unwrap();
        assert_eq!(hmac.finalize(), expected);
        let mut copy = copy;
        copy.update(&message[split..]).unwrap();
        assert_eq!(copy.finalize(), expected);
    }
}

#[test]
fn random_keys_and_messages_match_cng() {
    let mut generator = Generator(0xfeed_face_2026_0004);
    for _ in 0..1500 {
        let key_length = (generator.next() % 300 + 1) as usize;
        let data_length = (generator.next() % 2000) as usize;
        let key = generator.bytes(key_length);
        let data = generator.bytes(data_length);
        assert_eq!(tag(&key, &data), cng(&key, &data));
    }
}

// ====== verifica ======

#[test]
fn verify_accepts_only_the_exact_tag() {
    let key = b"chiave di verifica";
    let data = b"messaggio autenticato";
    let good = tag(key, data);
    let fresh = || {
        let mut hmac = HmacSha256::new(key).unwrap();
        hmac.update(data).unwrap();
        hmac
    };
    assert!(fresh().verify(&good));
    for position in 0..TAG_BYTES {
        for bit in 0..8 {
            let mut bad = good;
            bad[position] ^= 1 << bit;
            assert!(!fresh().verify(&bad));
        }
    }
    assert!(!fresh().verify(&good[..31]));
    assert!(!fresh().verify(&good[..16]));
    assert!(!fresh().verify(&[]));
    let mut longer = good.to_vec();
    longer.push(0);
    assert!(!fresh().verify(&longer));
    assert!(!HmacSha256::new(b"altra chiave").unwrap().verify(&good));
}

// ====== azzeramento alla distruzione ======

#[test]
fn dropped_keyed_state_is_wiped() {
    let mut storage = std::mem::MaybeUninit::<HmacSha256>::uninit();
    let mut hmac = HmacSha256::new(&[0xa5; 80]).unwrap();
    hmac.update(b"dati").unwrap();
    storage.write(hmac);
    let bytes = unsafe {
        let pointer = storage.as_mut_ptr();
        std::ptr::drop_in_place(pointer);
        std::slice::from_raw_parts(pointer.cast::<u8>(), std::mem::size_of::<HmacSha256>()).to_vec()
    };
    assert_eq!(bytes.len(), 224);
    assert!(bytes.iter().all(|byte| *byte == 0));
}
