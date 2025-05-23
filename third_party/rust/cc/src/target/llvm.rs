/// Rust and Clang don't really agree on naming, so do a best-effort
/// conversion to support out-of-tree / custom target-spec targets.
pub(crate) fn guess_llvm_target_triple(
    full_arch: &str,
    vendor: &str,
    os: &str,
    env: &str,
    abi: &str,
) -> String {
    let arch = match full_arch {
        riscv32 if riscv32.starts_with("riscv32") => "riscv32",
        riscv64 if riscv64.starts_with("riscv64") => "riscv64",
        arch => arch,
    };
    let os = match os {
        "darwin" => "macosx",
        "visionos" => "xros",
        "uefi" => "windows",
        os => os,
    };
    let env = match env {
        "newlib" | "nto70" | "nto71" | "ohos" | "p1" | "p2" | "relibc" | "sgx" | "uclibc" => "",
        env => env,
    };
    let abi = match abi {
        "sim" => "simulator",
        "llvm" | "softfloat" | "uwp" | "vec-extabi" => "",
        "ilp32" => "_ilp32",
        abi => abi,
    };
    match (env, abi) {
        ("", "") => format!("{arch}-{vendor}-{os}"),
        (env, abi) => format!("{arch}-{vendor}-{os}-{env}{abi}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_llvm_triple_guessing() {
        assert_eq!(
            guess_llvm_target_triple("aarch64", "unknown", "linux", "", ""),
            "aarch64-unknown-linux"
        );
        assert_eq!(
            guess_llvm_target_triple("x86_64", "unknown", "linux", "gnu", ""),
            "x86_64-unknown-linux-gnu"
        );
        assert_eq!(
            guess_llvm_target_triple("x86_64", "unknown", "linux", "gnu", "eabi"),
            "x86_64-unknown-linux-gnueabi"
        );
        assert_eq!(
            guess_llvm_target_triple("x86_64", "apple", "darwin", "", ""),
            "x86_64-apple-macosx"
        );
    }
}
