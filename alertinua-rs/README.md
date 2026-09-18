# alertinua — порт з C (ESP-IDF/PlatformIO) на Rust

Rust-порт прошивки [tepasha/alertinua](https://github.com/tepasha/alertinua) для
LilyGO T-Display (ESP32 + дисплей ST7789 240x135), яка опитує
[alerts.in.ua](https://alerts.in.ua/) та показує статус повітряної тривоги.

## Архітектурне рішення

ESP-IDF залишено як фреймворк (Wi-Fi stack, HTTPS-клієнт з mbedTLS,
provisioning через HTTP-сервер, NVS) — переписувати ці підсистеми на
no_std Rust не було сенсу. Натомість увесь прикладний код написано на
Rust через офіційні крейти `esp-idf-hal` / `esp-idf-svc` (обгортка над
ESP-IDF в стилі `std`), а не напряму через `esp-idf-sys`/FFI.

Для дисплея замість прямих викликів `esp_lcd_panel_*` використано
ідіоматичний Rust embedded-стек: `esp-idf-hal` (SPI/GPIO) +
`display-interface-spi` + `mipidsi` (драйвер ST7789) +
`embedded-graphics`.

## Відповідність файлів C ↔ Rust

| Оригінал (C) | Rust | Примітка |
|---|---|---|
| `src/main.c` | `src/main.rs` | оркестрація `app_main()` |
| `components/button/*` | `src/button.rs` | watcher довгого натискання, окремий потік замість `xTaskCreate` |
| `components/buzzer/*` | `src/buzzer.rs` | LEDC PWM, 1:1 логіка сирени |
| `components/wifi_manager/*` (+ `wifi_creds.c`) | `src/wifi_manager.rs` + `src/wifi_setup_page.html` | STA-підключення, NVS, SoftAP provisioning, HTML-сторінка витягнута дослівно |
| `components/scraping/scraping.c` | `src/scraping.rs` | лише `api_fetch_bearer_auth()` — дубльований мертвий код (`get_api`, друга копія `wifi_connect_sta`) з оригіналу не переносився |
| `components/map/map_render.c` | `src/display.rs` (ініціалізація/вивід) + `src/map/render.rs` (растеризація) | розділено на "керування дисплеєм" і "чисту логіку малювання" |
| `components/map/ukraine_map_data.h` | `src/map/ukraine_map_data.rs` | згенеровано **скриптом** з оригінального `.h` (921 точка, 25 областей) — звірено кількість елементів 1:1 |
| `components/env/env.py` (PlatformIO hook) | `build.rs` | той самий принцип: читає `.env`, підставляє `ALERT_API`/`ALERT_TOKEN` на етапі збірки через `cargo:rustc-env=` замість `-D...` для C-макросів; у коді читається через `env!("ALERT_API")` |
| `platformio.ini` | `Cargo.toml` + `.cargo/config.toml` + `rust-toolchain.toml` | ціль `xtensa-esp32-espidf` (плата класична T-Display, не S3 — див. застереження нижче) |
| `sdkconfig.lilygo-t-display` | `sdkconfig.defaults` | скопійовано як є, ESP-IDF конфіг не залежить від мови прошивки |

## Збірка

Порт написаний і структурований, але **не скомпільований на реальному
ESP-таргеті** — у цьому середовищі немає мережевого доступу до
Espressif-репозиторіїв/toolchain (`espup`), тому збірку й фактичне
прошивання плати треба виконати на вашій машині:

```bash
# 1. встановити Rust-таргет і toolchain для ESP32 (один раз)
cargo install espup ldproxy espflash
espup install
. $HOME/export-esp.sh   # або export-esp.ps1 на Windows

# 2. підготувати секрети
cp .env.example .env
# відредагувати ALERT_API / ALERT_TOKEN у .env

# 3. збірка і прошивка
cargo build --release
espflash flash --monitor target/xtensa-esp32-espidf/release/alertinua
```

## Важливі застереження (перевірте перед прошивкою)

1. **Точні сигнатури `esp-idf-hal`/`esp-idf-svc`.** API цих крейтів
   змінюється між мінорними версіями (SPI-конструктори, `LedcDriver`,
   `EspHttpServer::fn_handler`, `EspNvs` тощо). Я звірив логіку 1:1 з
   оригінальним C, але не мав змоги прогнати `cargo build` на цільовому
   таргеті — тож перший запуск майже напевно вимагатиме дрібних
   правок під версії крейтів, які реально резолвнуться у вашому
   `Cargo.lock` (насамперед `src/display.rs` і `src/wifi_manager.rs`).
2. **Плата: T-Display (ESP32) vs T-Display-S3.** README оригінального
   репозиторію згадує "T-Display-S3 AMOLED", але піни в
   `map_render.c` (MOSI=19, SCLK=18, CS=5, DC=16, RST=23, BL=4) і
   `platformio.ini` (`board = lilygo-t-display`) відповідають
   **класичному** LilyGO T-Display на ESP32 (Xtensa) з дисплеєм
   ST7789 по SPI — це вже невідповідність в оригінальному C-проєкті,
   я переніс код "як є", орієнтуючись на реальні піни/board, а не на
   назву в README.
3. **Логіка позначення тривоги на мапі не була реалізована й у
   C-оригіналі.** `main.c` лише логував тіло відповіді API
   (`ESP_LOGI("Response body:\n%s", ...)`), не парсив JSON і не
   викликав `render_mark_region_red()`. Я зберіг це один в один і
   позначив місце в `main.rs` коментарем `TODO`, куди додати парсинг
   JSON від alerts.in.ua (структура регіонів вже готова в
   `map::ukraine_map_data::MAP_REGIONS`).
4. **`unsafe fn clone_unchecked()` на пінах/периферії** в `esp-idf-hal`
   існує саме для таких сценаріїв (Cargo-сінглтон `Peripherals`), але
   переконайтеся, що жоден пін не використовується двічі одночасно
   (я навмисно розв'язав це для Wi-Fi-модема через
   `Arc<Mutex<BlockingWifi<..>>>`, спільний між основним потоком і
   потоком кнопки).
