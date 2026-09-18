use std::fs;
use std::path::Path;

/// Читає `.env` (якщо є) і прокидає значення як `ALERT_API` / `ALERT_TOKEN`
/// у compile-time через `cargo:rustc-env=`. Це заміна `components/env/env.py`
/// з оригінального PlatformIO-проєкту: там значення підставлялись у C-код
/// через `-D`, тут — через `env!("ALERT_API")` у Rust-коді (див. src/main.rs).
fn load_dot_env() {
    let path = Path::new(".env");
    if !path.exists() {
        println!(
            "cargo:warning=.env not found — ALERT_API/ALERT_TOKEN will be empty, set them in .env"
        );
        return;
    }

    let contents = fs::read_to_string(path).expect("failed to read .env");
    for line in contents.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some((key, value)) = line.split_once('=') {
            let key = key.trim();
            let mut value = value.trim();
            if (value.starts_with('"') && value.ends_with('"'))
                || (value.starts_with('\'') && value.ends_with('\''))
            {
                value = &value[1..value.len() - 1];
            }
            println!("cargo:rustc-env={key}={value}");
        }
    }
    println!("cargo:rerun-if-changed=.env");
}

fn main() {
    load_dot_env();
    embuild::espidf::sysenv::output();
}
