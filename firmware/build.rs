use std::{env, fs};

const WIFI_KEYS: [&str; 2] = [
    "LED_ORCHESTRA_WIFI_SSID",
    "LED_ORCHESTRA_WIFI_PASSWORD",
];

fn main() {
    println!("cargo:rerun-if-changed=.env");

    let dotenv = read_dotenv();
    for key in WIFI_KEYS {
        println!("cargo:rerun-if-env-changed={key}");

        if let Ok(value) = env::var(key) {
            println!("cargo:rustc-env={key}={value}");
        } else if let Some(value) = dotenv
            .iter()
            .find_map(|(dotenv_key, value)| (dotenv_key == key).then_some(value))
        {
            println!("cargo:rustc-env={key}={value}");
        }
    }
}

fn read_dotenv() -> Vec<(String, String)> {
    let Ok(contents) = fs::read_to_string(".env") else {
        return Vec::new();
    };

    contents
        .lines()
        .filter_map(parse_dotenv_line)
        .collect()
}

fn parse_dotenv_line(line: &str) -> Option<(String, String)> {
    let line = line.trim();
    if line.is_empty() || line.starts_with('#') {
        return None;
    }

    let (key, value) = line.split_once('=')?;
    let key = key.trim().to_string();
    let value = value.trim();
    let value = value
        .strip_prefix('"')
        .and_then(|value| value.strip_suffix('"'))
        .or_else(|| {
            value
                .strip_prefix('\'')
                .and_then(|value| value.strip_suffix('\''))
        })
        .unwrap_or(value)
        .to_string();

    Some((key, value))
}
