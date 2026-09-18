//! HTTP-клієнт для опитування API тривог, портовано з
//! components/scraping/scraping.c (функція `api_fetch_bearer_auth`).
//!
//! Примітка: у C-оригіналі `scraping.c` також містив статичну,
//! непотрібну копію `wifi_connect_sta()`/`get_api()`, позначену
//! `[[maybe_unused]]` — та ж логіка, що й у `wifi_manager.c`. Тут цей
//! дубльований мертвий код не переносився, бо він ніде фактично не
//! викликався і в самому C-проєкті.

use std::io::Read;

use embedded_svc::http::client::Client as HttpClient;
use embedded_svc::http::Method;
use esp_idf_svc::http::client::{Configuration as HttpConfig, EspHttpConnection};

const MAX_HTTP_OUTPUT_BUFFER: usize = 2048;

/// Аналог `api_fetch_bearer_auth()`. Повертає (HTTP-статус, тіло відповіді).
pub fn fetch_bearer_auth(url: &str, token: &str) -> anyhow::Result<(u16, String)> {
    let connection = EspHttpConnection::new(&HttpConfig {
        crt_bundle_attach: Some(esp_idf_svc::sys::esp_crt_bundle_attach),
        timeout: Some(std::time::Duration::from_secs(10)),
        ..Default::default()
    })?;
    let mut client = HttpClient::wrap(connection);

    let auth_header = format!("Bearer {token}");
    let headers = [("Authorization", auth_header.as_str()), ("Accept", "application/json")];

    let request = client.request(Method::Get, url, &headers)?;
    let response = request.submit()?;
    let status = response.status();

    let mut body = Vec::new();
    let mut buf = [0u8; 256];
    let mut reader = response;
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        body.extend_from_slice(&buf[..n]);
        if body.len() >= MAX_HTTP_OUTPUT_BUFFER {
            break;
        }
    }

    let text = String::from_utf8_lossy(&body).to_string();
    log::info!("HTTP status: {status}, body length: {}", text.len());

    Ok((status, text))
}
