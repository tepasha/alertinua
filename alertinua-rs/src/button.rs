//! Watcher довгого натискання кнопки, портовано з components/button/button.c.

use esp_idf_hal::gpio::{AnyIOPin, Input, PinDriver, Pull};
use esp_idf_svc::hal::delay::FreeRtos;
use std::thread;

pub const SETUP_BUTTON_GPIO: i32 = 0;
pub const LONG_PRESS_MS: u32 = 3000; // тримати 3с, щоб увійти в налаштування Wi-Fi

const POLL_MS: u32 = 50;

/// Запускає фонову задачу (окремий потік/FreeRTOS task), яка стежить за
/// довгим натисканням кнопки на вказаному GPIO і викликає `on_long_press`,
/// коли поріг досягнуто. Аналог `button_start_long_press_watch()` з C.
pub fn start_long_press_watch(
    pin: AnyIOPin,
    long_press_ms: u32,
    on_long_press: impl Fn() + Send + 'static,
) -> anyhow::Result<()> {
    let mut driver = PinDriver::input(pin)?;
    driver.set_pull(Pull::Up)?;

    thread::Builder::new()
        .stack_size(2048)
        .spawn(move || {
            let mut held_ms: u32 = 0;
            let mut fired = false;

            loop {
                let pressed = driver.is_low();

                if pressed {
                    held_ms += POLL_MS;
                    if held_ms >= long_press_ms && !fired {
                        fired = true;
                        log::info!("long press detected on GPIO{}", SETUP_BUTTON_GPIO);
                        on_long_press();
                    }
                } else {
                    held_ms = 0;
                    fired = false;
                }

                FreeRtos::delay_ms(POLL_MS);
            }
        })
        .map_err(|e| anyhow::anyhow!("failed to spawn button task: {e}"))?;

    Ok(())
}
