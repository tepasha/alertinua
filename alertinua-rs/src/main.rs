mod button;
mod buzzer;
mod display;
mod map;
mod scraping;
mod wifi_manager;

use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::peripherals::Peripherals;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sys::link_patches;

/// Значення підставляються на етапі збірки з `.env` через `build.rs`
/// (`cargo:rustc-env=...`) — заміна `-D ALERT_API=...` з PlatformIO.
const ALERT_API: &str = env!("ALERT_API", "set ALERT_API in .env — see build.rs");
const ALERT_TOKEN: &str = env!("ALERT_TOKEN", "set ALERT_TOKEN in .env — see build.rs");

fn main() -> anyhow::Result<()> {
    link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    let mut peripherals = Peripherals::take()?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = EspDefaultNvsPartition::take()?;

    // ініціалізація дисплея + framebuffer, як у app_main() з main.c
    let mut disp = display::display_init(&mut peripherals)?;
    let mut fb = vec![0u16; map::FB_LEN];

    log::info!("Read API: {ALERT_API}, Token: {ALERT_TOKEN}");
    if ALERT_API.is_empty() || ALERT_TOKEN.is_empty() {
        log::error!("Can't read API and Token");
        map::render::draw_err_banner(&mut fb);
    }

    let (wifi, connected) = wifi_manager::connect_sta(peripherals.modem, sys_loop.clone(), nvs.clone())?;
    // Той самий Wi-Fi-хендл живе далі в `Arc<Mutex<_>>`, щоб замикання
    // кнопки (яке спрацьовує пізніше, в іншому потоці) могло перевести
    // його в SoftAP-режим для provisioning без повторного захоплення
    // `Peripherals::modem` (той є одноразовим сінглтоном в esp-idf-hal).
    let wifi = std::sync::Arc::new(std::sync::Mutex::new(wifi));

    if connected {
        match scraping::fetch_bearer_auth(ALERT_API, ALERT_TOKEN) {
            Ok((status, body)) if (200..300).contains(&status) => {
                log::info!("Response body:\n{body}");
                // TODO: розпарсити body (JSON alerts.in.ua) і викликати
                // map::render::render_map_multicolor / mark_region_red
                // для позначення областей з тривогою — у C-оригіналі
                // цей крок так само лишався на майбутнє (main.c тільки
                // виводив тіло відповіді в лог).
                map::render::render_map(&mut fb, None);
            }
            Ok((status, _)) => {
                log::error!("Fetch failed, status: {status}");
                map::render::draw_err_banner(&mut fb);
            }
            Err(e) => {
                log::error!("Fetch failed: {e}");
                map::render::draw_err_banner(&mut fb);
            }
        }
    } else {
        log::error!(
            "Failed to connect to WiFi. Hold the setup button for {}s to reconfigure.",
            button::LONG_PRESS_MS / 1000
        );
        map::render::draw_err_banner(&mut fb);

        let nvs_for_ap = nvs.clone();
        let wifi_for_ap = wifi.clone();
        let setup_pin: esp_idf_hal::gpio::AnyIOPin = peripherals.pins.gpio0.into();

        button::start_long_press_watch(setup_pin, button::LONG_PRESS_MS, move || {
            let mut wifi = wifi_for_ap.lock().unwrap();
            if let Err(e) = wifi_manager::start_provisioning(&mut wifi, nvs_for_ap.clone()) {
                log::error!("failed to start provisioning: {e}");
            }
        })?;
    }

    core::mem::forget(wifi); // тримаємо Wi-Fi живим, поки працює програма

    display::blit(&mut disp, &fb)?;

    // app_main() у C-версії завершується тут (одноразовий запуск+малюнок).
    // FreeRTOS-задачі (button watcher) продовжують жити у фоні.
    loop {
        esp_idf_svc::hal::delay::FreeRtos::delay_ms(1000);
    }
}
