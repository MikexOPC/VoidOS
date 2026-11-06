#[path = "../../../../../../lib/security/hash/sha256/rust/sha256.rs"]
mod sha256;

use sha256::*;

// ====== supporto ======

fn hex(text: &str) -> [u8; DIGEST_BYTES] {
    let clean: Vec<u8> = text.bytes().filter(|byte| !byte.is_ascii_whitespace()).collect();
    let mut digest = [0u8; DIGEST_BYTES];
    for (byte, pair) in digest.iter_mut().zip(clean.as_chunks::<2>().0) {
        *byte = u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap();
    }
    digest
}

fn hash(data: &[u8]) -> [u8; DIGEST_BYTES] {
    digest(data).expect("lunghezza ammessa")
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

const BCRYPT_SHA256_ALG_HANDLE: usize = 0x41;

fn cng(data: &[u8]) -> [u8; DIGEST_BYTES] {
    let mut output = [0u8; DIGEST_BYTES];
    let length = u32::try_from(data.len()).unwrap();
    let status = unsafe {
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, std::ptr::null(), 0, data.as_ptr(), length, output.as_mut_ptr(), DIGEST_BYTES as u32)
    };
    assert_eq!(status, 0);
    output
}

// ====== costanti ricavate dai numeri primi ======

fn primes(count: usize) -> Vec<u128> {
    let mut found = Vec::new();
    let mut candidate = 2u128;
    while found.len() < count {
        if found.iter().all(|prime| !candidate.is_multiple_of(*prime)) {
            found.push(candidate);
        }
        candidate += 1;
    }
    found
}

fn integer_root(value: u128, degree: u32) -> u128 {
    let (mut low, mut high) = (0u128, 1u128 << (128 / degree));
    while low < high {
        let middle = (low + high).div_ceil(2);
        if middle.checked_pow(degree).is_some_and(|power| power <= value) { low = middle } else { high = middle - 1 }
    }
    low
}

#[test]
fn constants_match_their_definition_in_fips_180_4() {
    for (constant, prime) in ROUND_CONSTANTS.iter().zip(primes(64)) {
        assert_eq!(*constant, integer_root(prime << 96, 3) as u32);
    }
    for (initial, prime) in INITIAL_HASH.iter().zip(primes(8)) {
        assert_eq!(*initial, integer_root(prime << 64, 2) as u32);
    }
}

// ====== vettori NIST ======

#[test]
fn nist_example_messages() {
    assert_eq!(hash(b""), hex("e3b0c442 98fc1c14 9afbf4c8 996fb924 27ae41e4 649b934c a495991b 7852b855"));
    assert_eq!(hash(b"abc"), hex("ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad"));
    assert_eq!(
        hash(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        hex("248d6a61 d20638b8 e5c02693 0c3e6039 a33ce459 64ff2167 f6ecedd4 19db06c1"),
    );
    assert_eq!(
        hash(b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
        hex("cf5b16a7 78af8380 036ce59e 7b049237 0b249b11 e8f07a51 afac4503 7afee9d1"),
    );
    let mut million = Sha256::new();
    for _ in 0..1000 {
        million.update(&[b'a'; 1000]).unwrap();
    }
    assert_eq!(million.finalize(), hex("cdc76e5c 9914fb92 81a1c7e2 84d73e67 f1809a48 a497200e 046d39cc c7112cd0"));
}

#[test]
fn nist_vectors_agree_with_cng() {
    for message in [&b""[..], b"abc", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"] {
        assert_eq!(hash(message), cng(message));
    }
    let million = vec![b'a'; 1_000_000];
    assert_eq!(hash(&million), cng(&million));
}

// ====== confini del padding ======

#[test]
fn every_length_up_to_five_blocks_matches_cng() {
    let mut generator = Generator(0x5a17_2026_1107_0001);
    for length in 0..=320 {
        let message = generator.bytes(length);
        assert_eq!(hash(&message), cng(&message), "lunghezza {length}");
    }
    for length in [55, 56, 63, 64, 65, 119, 120, 127, 128] {
        assert_eq!(hash(&vec![0u8; length]), cng(&vec![0u8; length]));
        assert_eq!(hash(&vec![0xffu8; length]), cng(&vec![0xffu8; length]));
    }
}

// ====== aggiornamenti a pezzi ======

#[test]
fn streaming_matches_one_shot_for_any_split() {
    let mut generator = Generator(0x0bad_cafe_2026_0002);
    let message = generator.bytes(1000);
    let expected = hash(&message);
    let mut single = Sha256::new();
    for byte in &message {
        single.update(std::slice::from_ref(byte)).unwrap();
    }
    assert_eq!(single.finalize(), expected);
    for split in 0..=message.len() {
        let mut hasher = Sha256::new();
        hasher.update(&message[..split]).unwrap();
        hasher.update(&[]).unwrap();
        hasher.update(&message[split..]).unwrap();
        assert_eq!(hasher.finalize(), expected, "taglio {split}");
    }
}

#[test]
fn random_messages_and_chunks_match_cng() {
    let mut generator = Generator(0x1234_5678_9abc_def1);
    for _ in 0..2000 {
        let length = (generator.next() % 5000) as usize;
        let message = generator.bytes(length);
        let mut hasher = Sha256::new();
        let mut offset = 0;
        while offset < message.len() {
            let step = ((generator.next() % 200) as usize).min(message.len() - offset);
            hasher.update(&message[offset..offset + step]).unwrap();
            offset += step;
        }
        assert_eq!(hasher.finalize(), cng(&message));
    }
}

#[test]
fn clones_continue_independently() {
    let mut first = Sha256::new();
    first.update(b"prefisso comune ").unwrap();
    let mut second = first.clone();
    first.update(b"uno").unwrap();
    second.update(b"due").unwrap();
    assert_eq!(first.finalize(), hash(b"prefisso comune uno"));
    assert_eq!(second.finalize(), hash(b"prefisso comune due"));
    assert_eq!(Sha256::default().finalize(), hash(b""));
}

// ====== azzeramento alla distruzione ======

#[test]
fn dropped_state_is_wiped() {
    let mut storage = std::mem::MaybeUninit::<Sha256>::uninit();
    let mut hasher = Sha256::new();
    hasher.update(&[0xa5; 100]).unwrap();
    storage.write(hasher);
    let bytes = unsafe {
        let pointer = storage.as_mut_ptr();
        std::ptr::drop_in_place(pointer);
        std::slice::from_raw_parts(pointer.cast::<u8>(), std::mem::size_of::<Sha256>())
    };
    assert_eq!(std::mem::size_of::<Sha256>(), 112);
    assert!(bytes.iter().all(|byte| *byte == 0));
}

// ====== limite di 2^64 bit ======

#[test]
fn message_length_limit_is_enforced_without_side_effects() {
    let mut near = Sha256::with_length(MAXIMUM_MESSAGE_BYTES - 3);
    assert_eq!(near.update(b"abc"), Ok(()));
    assert_eq!(near.update(&[]), Ok(()));
    let before = near.clone().finalize();
    assert_eq!(near.update(b"x"), Err(Sha256Error::MessageTooLong));
    assert_eq!(near.clone().finalize(), before);
    let mut full = Sha256::with_length(MAXIMUM_MESSAGE_BYTES);
    assert_eq!(full.update(b"x"), Err(Sha256Error::MessageTooLong));
    let mut wrapping = Sha256::with_length(u64::MAX - 1);
    assert_eq!(wrapping.update(b"xyz"), Err(Sha256Error::MessageTooLong));
    assert_eq!(MAXIMUM_MESSAGE_BYTES.checked_mul(8).map(|bits| bits < u64::MAX), Some(true));
    assert!(u128::from(MAXIMUM_MESSAGE_BYTES + 1) * 8 > u128::from(u64::MAX));
}
