fn main() {
    println!("cargo:rerun-if-env-changed=HRTPRINT_VERSION");
    let version = std::env::var("HRTPRINT_VERSION")
        .unwrap_or_else(|_| std::env::var("CARGO_PKG_VERSION").expect("CARGO_PKG_VERSION"));
    println!("cargo:rustc-env=HRTPRINT_VERSION={version}");
}
