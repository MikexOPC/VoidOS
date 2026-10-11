#![deny(unsafe_op_in_unsafe_fn)]
#[cfg(not(all(target_arch = "x86_64", target_os = "windows", target_env = "msvc")))]
compile_error!("Questi test FFI richiedono Windows x64 MSVC.");

mod ffi;
mod firmware;
use ffi::{DEVICE_ERROR, INVALID, NX, OUT_OF_RESOURCES, STACK_BASE, SUCCESS, Tables, UNSUPPORTED};
use std::collections::BTreeSet;

// ====== confronto del proprietario C con il modello indipendente Rust ====== //

fn assert_ownership(tables: &Tables) {
    assert!(tables.count <= 128);
    let addresses: BTreeSet<_> = tables.pages[..tables.count as usize]
        .iter()
        .copied()
        .filter(|address| *address != 0)
        .collect();
    let nonzero = tables.pages[..tables.count as usize]
        .iter()
        .filter(|address| **address != 0)
        .count();
    assert_eq!(
        addresses.len(),
        nonzero,
        "indirizzi duplicati nel proprietario C"
    );
    firmware::with(|model| {
        assert_eq!(
            addresses,
            model.addresses(),
            "proprieta C diversa dalle pagine vive"
        );
        assert_eq!(model.allocations, model.released + addresses.len());
        assert_eq!(
            model.invalid_calls, 0,
            "rilascio doppio, estraneo o callback invalido"
        );
        assert!(model.guards_intact(), "scrittura oltre la pagina allocata");
    });
}

fn finish(tables: &mut Tables) {
    firmware::with(|model| model.fail_release.clear());
    assert_eq!(ffi::release(tables), SUCCESS);
    assert_ownership(tables);
    assert_eq!((tables.root, tables.valid, tables.count), (0, 0, 0));
    let attempts = firmware::with(|model| model.release_attempts);
    assert_eq!(ffi::release(tables), SUCCESS);
    assert_eq!(firmware::with(|model| model.release_attempts), attempts);
}

// ====== lettura delle tabelle tramite indirizzi posseduti dal modello ====== //

fn walk(root: u64, address: u64) -> Option<u64> {
    firmware::with(|model| {
        let mut current = root;
        for (level, shift) in [39, 30, 21, 12].into_iter().enumerate() {
            let entry = model
                .entry(current, ((address >> shift) & 511) as usize)
                .expect("riferimento a una tabella non posseduta");
            if entry & 1 == 0 {
                assert_eq!(entry, 0, "voce assente non azzerata");
                return None;
            }
            if level == 3 {
                return Some(entry);
            }
            assert_eq!(entry & 0xfff, 3, "permessi intermedi o huge page inattesi");
            assert_eq!(entry & NX, 0);
            current = entry & 0x000f_ffff_ffff_f000;
        }
        unreachable!()
    })
}

#[test]
fn abi_matches_c_headers() {
    assert!(ffi::layout_matches());
}

#[test]
fn mappings_permissions_and_unmapped_guards() {
    firmware::reset();
    let mut tables = Tables::default();
    assert_eq!(ffi::build(&mut tables, 52, 1), SUCCESS);
    assert_eq!(tables.valid, 1);
    assert_ownership(&tables);
    for (virtual_base, physical_base, pages, permissions) in [
        (0x1ff000, 0x400000, 2, 1),
        (0x3ffff000, 0x500000, 2, NX | 3),
        ((1u64 << 39) - 4096, 0x600000, 2, NX | 1),
        (STACK_BASE, 0x800000, 16, NX | 3),
    ] {
        for page in 0..pages {
            assert_eq!(
                walk(tables.root, virtual_base + page * 4096),
                Some((physical_base + page * 4096) | permissions)
            );
        }
    }
    for address in [0, STACK_BASE - 4096, STACK_BASE + 65536, 0x400000] {
        assert_eq!(walk(tables.root, address), None);
    }
    let before = firmware::with(|model| model.allocations);
    assert_eq!(ffi::build(&mut tables, 52, 1), INVALID);
    assert_eq!(firmware::with(|model| model.allocations), before);
    finish(&mut tables);
}

#[test]
fn every_allocation_failure_retains_exactly_the_acquired_pages() {
    firmware::reset();
    let mut reference = Tables::default();
    assert_eq!(ffi::build(&mut reference, 52, 1), SUCCESS);
    let needed = firmware::with(|model| model.allocations);
    finish(&mut reference);
    for fail_at in 0..needed {
        firmware::reset();
        firmware::with(|model| model.fail_allocation = Some(fail_at));
        let mut tables = Tables::default();
        assert_eq!(
            ffi::build(&mut tables, 52, 1),
            OUT_OF_RESOURCES,
            "allocazione {fail_at}"
        );
        assert_eq!((tables.root, tables.valid), (0, 0));
        assert_eq!(tables.count as usize, fail_at);
        assert_ownership(&tables);
        finish(&mut tables);
    }
}

#[test]
fn every_release_failure_is_retryable_without_double_free() {
    firmware::reset();
    let mut reference = Tables::default();
    assert_eq!(ffi::build(&mut reference, 52, 1), SUCCESS);
    let needed = reference.count;
    finish(&mut reference);
    for index in 0..needed {
        firmware::reset();
        let mut tables = Tables::default();
        assert_eq!(ffi::build(&mut tables, 52, 1), SUCCESS);
        let failed = tables.pages[index as usize];
        firmware::with(|model| {
            model.fail_release.insert(failed);
        });
        assert_eq!(ffi::release(&mut tables), DEVICE_ERROR);
        assert_eq!((tables.root, tables.valid), (0, 0));
        assert_ownership(&tables);
        assert_eq!(
            firmware::with(|model| model.addresses()),
            BTreeSet::from([failed])
        );
        let attempts = firmware::with(|model| model.release_attempts);
        assert_eq!(ffi::release(&mut tables), DEVICE_ERROR);
        assert_eq!(firmware::with(|model| model.release_attempts), attempts + 1);
        assert_ownership(&tables);
        finish(&mut tables);
    }
}

#[test]
fn unsupported_capabilities_allocate_nothing() {
    for (bits, nx) in [(31, 1), (53, 1), (52, 0), (52, 2)] {
        firmware::reset();
        let mut tables = Tables::default();
        assert_eq!(ffi::build(&mut tables, bits, nx), UNSUPPORTED);
        assert_eq!(firmware::with(|model| model.allocations), 0);
        assert_ownership(&tables);
        finish(&mut tables);
    }
}

#[test]
fn duplicate_firmware_output_does_not_create_a_second_owner() {
    firmware::reset();
    firmware::with(|model| model.duplicate_output = true);
    let mut tables = Tables::default();
    assert_eq!(ffi::build(&mut tables, 52, 1), DEVICE_ERROR);
    assert_eq!(tables.count, 1);
    assert_ownership(&tables);
    finish(&mut tables);
}

// ====== sequenze riproducibili di costruzione errore e tentativi di rilascio ====== //

#[test]
fn generated_failure_sequences_preserve_ownership() {
    let mut state = 0x7a68_9c42_d53e_b107u64;
    for scenario in 0..512 {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        firmware::reset();
        let fail_at = ((state >> 32) % 20) as usize;
        firmware::with(|model| model.fail_allocation = Some(fail_at));
        let mut tables = Tables::default();
        let result = ffi::build(&mut tables, 52, 1);
        assert!(
            result == SUCCESS || result == OUT_OF_RESOURCES,
            "sequenza {scenario}"
        );
        assert_ownership(&tables);
        let owned = firmware::with(|model| model.addresses());
        let failures: BTreeSet<_> = owned
            .iter()
            .enumerate()
            .filter(|(index, _)| (state.rotate_right(*index as u32) & 1) != 0)
            .map(|(_, address)| *address)
            .collect();
        firmware::with(|model| model.fail_release = failures.clone());
        assert_eq!(
            ffi::release(&mut tables),
            if failures.is_empty() {
                SUCCESS
            } else {
                DEVICE_ERROR
            }
        );
        assert_ownership(&tables);
        assert_eq!(firmware::with(|model| model.addresses()), failures);
        let attempts = firmware::with(|model| model.release_attempts);
        assert_eq!(
            ffi::release(&mut tables),
            if failures.is_empty() {
                SUCCESS
            } else {
                DEVICE_ERROR
            }
        );
        assert_eq!(
            firmware::with(|model| model.release_attempts),
            attempts + failures.len()
        );
        assert_ownership(&tables);
        finish(&mut tables);
    }
}
