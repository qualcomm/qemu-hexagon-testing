// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let asm_dir = manifest_dir.join("asm");

    let clang = env::var("HEXAGON_CLANG").unwrap_or_else(|_| "hexagon-clang".to_string());

    // Compile crt0.S
    let crt0_src = asm_dir.join("crt0.S");
    let crt0_obj = out_dir.join("crt0.o");

    let status = Command::new(&clang)
        .args(["-mv81", "-G0", "-c", "-o"])
        .arg(&crt0_obj)
        .arg(&crt0_src)
        .status()
        .expect("Failed to run hexagon-clang");
    assert!(status.success(), "Failed to compile crt0.S");

    // Compile pte.S (TLB page table)
    let pte_src = asm_dir.join("pte.S");
    let pte_obj = out_dir.join("pte.o");

    let status = Command::new(&clang)
        .args(["-mv81", "-G0", "-c", "-o"])
        .arg(&pte_obj)
        .arg(&pte_src)
        .status()
        .expect("Failed to run hexagon-clang");
    assert!(status.success(), "Failed to compile pte.S");

    // Linker script
    let linker_script = manifest_dir.join("hexagon.ld");
    println!("cargo:rustc-link-arg=-T{}", linker_script.display());

    // Tell cargo to link all object files
    println!("cargo:rustc-link-arg={}", crt0_obj.display());
    println!("cargo:rustc-link-arg={}", pte_obj.display());

    // Rerun if assembly, linker script, or toolchain path changes
    println!("cargo:rerun-if-changed=asm/crt0.S");
    println!("cargo:rerun-if-changed=asm/pte.S");
    println!("cargo:rerun-if-changed=hexagon.ld");
    println!("cargo:rerun-if-env-changed=HEXAGON_CLANG");
}
