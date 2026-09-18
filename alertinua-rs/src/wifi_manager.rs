//! Керування Wi-Fi: підключення в режимі станції зі збереженими (NVS) або
//! дефолтними обліковими даними, і SoftAP-режим налаштування зі сторінкою
//! вводу SSID/пароля. Портовано з components/wifi_manager/wifi_manager.c
//! та components/wifi_manager/wifi_creds.c.
//!
//! На відміну від оригіналу (низькорівневі esp_wifi_* / esp_event_handler_*
//! виклики), тут використано `esp_idf_svc::wifi::EspWifi` та
//! `esp_idf_svc::nvs`, які інкапсулюють ту саму подієву модель ESP-IDF.

use std::sync::Arc;
use std::time::Duration;

use embedded_svc::wifi::{AccessPointConfiguration, AuthMethod, ClientConfiguration, Configuration};
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::modem::Modem;
use esp_idf_svc::http::server::{Configuration as HttpServerConfig, EspHttpServer};
use esp_idf_svc::io::Write;
use esp_idf_svc::nvs::{EspDefaultNvsPartition, EspNvs, NvsDefault};
use esp_idf_svc::wifi::{BlockingWifi, EspWifi};

const DEFAULT_WIFI_SSID: &str = "your-wifi-ssid";
const DEFAULT_WIFI_PASSWORD: &str = "your-wifi-password";
const WIFI_MAX_RETRY: u8 = 5;

const PROVISIONING_AP_SSID: &str = "ESP32-Setup";
const PROVISIONING_AP_PASSWORD: &str = "";

const NVS_NAMESPACE: &str = "wifi_cfg";
const NVS_KEY_SSID: &str = "ssid";
const NVS_KEY_PASS: &str = "pass";

const SETTINGS_PAGE: &str = include_str!("wifi_setup_page.html");

/// Аналог `wifi_creds_load()`.
fn creds_load(nvs: &EspDefaultNvsPartition) -> Option<(String, String)> {
    let handle = EspNvs::new(nvs.clone(), NVS_NAMESPACE, false).ok()?;
    let mut ssid_buf = [0u8; 33];
    let mut pass_buf = [0u8; 65];
    let ssid = handle.get_str(NVS_KEY_SSID, &mut ssid_buf).ok()??;
    let pass = handle.get_str(NVS_KEY_PASS, &mut pass_buf).ok()??;
    Some((ssid.to_string(), pass.to_string()))
}

/// Аналог `wifi_creds_save()`.
fn creds_save(nvs: &EspDefaultNvsPartition, ssid: &str, password: &str) -> anyhow::Result<()> {
    let mut handle: EspNvs<NvsDefault> = EspNvs::new(nvs.clone(), NVS_NAMESPACE, true)?;
    handle.set_str(NVS_KEY_SSID, ssid)?;
    handle.set_str(NVS_KEY_PASS, password)?;
    Ok(())
}

/// Аналог `wifi_manager_connect_sta()`.
///
/// На відміну від C-версії (яка повертає лише `bool` і лишає весь Wi-Fi
/// стан у глобальних статичних змінних ESP-IDF), тут повертається сам
/// `BlockingWifi`-хендл разом з прапорцем успіху. Це зроблено навмисно:
/// той самий хендл потім можна перевести в режим SoftAP для provisioning
/// (`start_provisioning`) без повторного захоплення `Peripherals::modem`,
/// яке в esp-idf-hal є одноразовим сінглтоном.
pub fn connect_sta(
    modem: Modem,
    sys_loop: EspSystemEventLoop,
    nvs: EspDefaultNvsPartition,
) -> anyhow::Result<(BlockingWifi<EspWifi<'static>>, bool)> {
    let (ssid, password) = match creds_load(&nvs) {
        Some((ssid, pass)) => {
            log::info!("using WiFi credentials saved in NVS (SSID \"{ssid}\")");
            (ssid, pass)
        }
        None => {
            log::error!(
                "no saved WiFi credentials, using compiled-in default (SSID \"{DEFAULT_WIFI_SSID}\")"
            );
            (DEFAULT_WIFI_SSID.to_string(), DEFAULT_WIFI_PASSWORD.to_string())
        }
    };

    let esp_wifi = EspWifi::new(modem, sys_loop.clone(), Some(nvs))?;
    let mut wifi = BlockingWifi::wrap(esp_wifi, sys_loop)?;

    wifi.set_configuration(&Configuration::Client(ClientConfiguration {
        ssid: ssid.as_str().try_into().unwrap_or_default(),
        password: password.as_str().try_into().unwrap_or_default(),
        auth_method: AuthMethod::WPA2Personal,
        ..Default::default()
    }))?;

    wifi.start()?;
    log::info!("connecting to WiFi \"{ssid}\"...");

    let mut connected = false;
    for attempt in 1..=WIFI_MAX_RETRY {
        match wifi.connect() {
            Ok(()) => {
                connected = true;
                break;
            }
            Err(e) => {
                log::info!("retrying WiFi connection ({attempt}/{WIFI_MAX_RETRY}): {e}");
            }
        }
    }

    if connected && wifi.wait_netif_up().is_err() {
        connected = false;
    }

    Ok((wifi, connected))
}

/// Аналог `wifi_manager_start_provisioning()`: піднімає SoftAP
/// "ESP32-Setup" і веб-сторінку на http://192.168.4.1/ для введення
/// SSID/пароля, які зберігаються в NVS, після чого пристрій перезавантажується.
pub fn start_provisioning(
    wifi: &mut BlockingWifi<EspWifi<'static>>,
    nvs: EspDefaultNvsPartition,
) -> anyhow::Result<()> {
    log::info!("entering WiFi provisioning mode (SoftAP \"{PROVISIONING_AP_SSID}\")");

    wifi.stop()?;

    let auth_method = if PROVISIONING_AP_PASSWORD.is_empty() {
        AuthMethod::None
    } else {
        AuthMethod::WPA2Personal
    };

    wifi.set_configuration(&Configuration::AccessPoint(AccessPointConfiguration {
        ssid: PROVISIONING_AP_SSID.try_into().unwrap_or_default(),
        password: PROVISIONING_AP_PASSWORD.try_into().unwrap_or_default(),
        channel: 1,
        max_connections: 4,
        auth_method,
        ..Default::default()
    }))?;

    wifi.start()?;

    let nvs = Arc::new(nvs);
    let mut server = EspHttpServer::new(&HttpServerConfig::default())?;

    server.fn_handler("/", esp_idf_svc::http::Method::Get, move |req| {
        let mut resp = req.into_ok_response()?;
        resp.write_all(SETTINGS_PAGE.as_bytes())?;
        Ok::<(), anyhow::Error>(())
    })?;

    let nvs_for_save = nvs.clone();
    server.fn_handler("/save", esp_idf_svc::http::Method::Post, move |mut req| {
        let mut body = vec![0u8; 512];
        let content_len = req.content_len().unwrap_or(0) as usize;
        if content_len == 0 || content_len >= body.len() {
            req.into_status_response(400)?
                .write_all(b"Invalid form data")?;
            return Ok::<(), anyhow::Error>(());
        }

        let mut received = 0;
        while received < content_len {
            let n = req.read(&mut body[received..content_len])?;
            if n == 0 {
                break;
            }
            received += n;
        }
        let body_str = String::from_utf8_lossy(&body[..received]).to_string();

        let ssid = form_get_field(&body_str, "ssid");
        let password = form_get_field(&body_str, "password").unwrap_or_default();

        let Some(ssid) = ssid.filter(|s| !s.is_empty()) else {
            req.into_status_response(400)?.write_all(b"SSID is required")?;
            return Ok(());
        };

        match creds_save(&nvs_for_save, &ssid, &password) {
            Ok(()) => {
                log::info!("saved new WiFi credentials for SSID \"{ssid}\", rebooting...");
                let mut resp = req.into_ok_response()?;
                resp.write_all(b"<html><body><h2>Saved. Rebooting...</h2></body></html>")?;
                std::thread::sleep(Duration::from_millis(1000)); // дати відповіді піти в мережу
                unsafe { esp_idf_svc::sys::esp_restart() };
            }
            Err(e) => {
                log::error!("failed to save WiFi credentials: {e}");
                req.into_status_response(500)?
                    .write_all(b"Failed to save credentials")?;
            }
        }
        Ok(())
    })?;

    log::info!(
        "settings page ready: connect to \"{PROVISIONING_AP_SSID}\" and open http://192.168.4.1/"
    );

    // Сервер живе, поки не буде дропнутий; у оригіналі це теж "fire and
    // forget" (`static` `httpd_handle_t`) — навмисно "просочуємо" хендл,
    // щоб він жив до перезавантаження пристрою через /save.
    core::mem::forget(server);

    Ok(())
}

/// Аналог `url_decode()` + `form_get_field()` з wifi_manager.c.
fn form_get_field(body: &str, key: &str) -> Option<String> {
    for pair in body.split('&') {
        if let Some((k, v)) = pair.split_once('=') {
            if k == key {
                return Some(url_decode(v));
            }
        }
    }
    None
}

fn url_decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => {
                out.push(b' ');
                i += 1;
            }
            b'%' if i + 2 < bytes.len() => {
                if let Ok(byte) = u8::from_str_radix(&s[i + 1..i + 3], 16) {
                    out.push(byte);
                    i += 3;
                } else {
                    out.push(bytes[i]);
                    i += 1;
                }
            }
            b => {
                out.push(b);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).to_string()
}
