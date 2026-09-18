//! Ініціалізація дисплея ST7789 (LilyGO T-Display, SPI) та вивід framebuffer.
//! Портовано з `display_init()` у components/map/map_render.c.
//!
//! Оригінал працював напряму з `esp_lcd_panel_*` (C API ESP-IDF). Тут
//! замість цього використано ідіоматичний для Rust embedded стек:
//! `esp-idf-hal` для SPI/GPIO + `display-interface-spi` + `mipidsi` (driver
//! для ST7789) + `embedded-graphics`. Це не C-биндинги, а окремий,
//! добре підтримуваний Rust-крейт для саме цієї матриці дисплеїв.

use embedded_graphics::image::ImageRawBE;
use embedded_graphics::pixelcolor::Rgb565;
use embedded_graphics::prelude::*;
use esp_idf_hal::delay::Ets;
use esp_idf_hal::gpio::{Output, PinDriver};
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::spi::{config::Config as SpiConfig, SpiDeviceDriver, SpiDriverConfig};
use esp_idf_hal::units::FromValueType;
use mipidsi::{models::ST7789, options::ColorOrder, Builder};

use crate::map::{MAP_DISPLAY_H, MAP_DISPLAY_W};

// Піни LilyGO T-Display, як у оригінальному map_render.c.
// PIN_MOSI=19 PIN_SCLK=18 PIN_CS=5 PIN_DC=16 PIN_RST=23 PIN_BL=4

pub type LcdDisplay<'a> = mipidsi::Display<
    display_interface_spi::SPIInterface<
        SpiDeviceDriver<'a, esp_idf_hal::spi::SpiDriver<'a>>,
        PinDriver<'a, esp_idf_hal::gpio::Gpio16, Output>,
    >,
    ST7789,
    PinDriver<'a, esp_idf_hal::gpio::Gpio23, Output>,
>;

pub fn display_init(peripherals: &mut Peripherals) -> anyhow::Result<LcdDisplay<'static>> {
    // Підсвітка (PIN_BL, GPIO4) — увімкнена одразу, як у оригіналі.
    let mut backlight = PinDriver::output(unsafe { peripherals.pins.gpio4.clone_unchecked() })?;
    backlight.set_high()?;
    core::mem::forget(backlight); // тримаємо пін увімкненим до кінця роботи програми

    let dc = PinDriver::output(unsafe { peripherals.pins.gpio16.clone_unchecked() })?;
    let rst = PinDriver::output(unsafe { peripherals.pins.gpio23.clone_unchecked() })?;

    let spi = SpiDeviceDriver::new_single(
        unsafe { peripherals.spi2.clone_unchecked() },
        unsafe { peripherals.pins.gpio18.clone_unchecked() }, // SCLK
        unsafe { peripherals.pins.gpio19.clone_unchecked() }, // MOSI
        Option::<esp_idf_hal::gpio::AnyIOPin>::None,          // MISO — не використовується
        Some(unsafe { peripherals.pins.gpio5.clone_unchecked() }), // CS
        &SpiDriverConfig::new(),
        &SpiConfig::new().baudrate(20.MHz().into()),
    )?;

    let di = display_interface_spi::SPIInterface::new(spi, dc);

    let display = Builder::new(ST7789, di)
        .reset_pin(rst)
        .display_size(MAP_DISPLAY_W as u16, MAP_DISPLAY_H as u16)
        // Панель фізично 135x240 (портрет); повертаємо в альбомну орієнтацію
        // 240x135, під яку згенеровано координати мапи (swap_xy(true) +
        // mirror(false, true) в оригіналі).
        .orientation(mipidsi::options::Orientation::new(mipidsi::options::Rotation::Deg90))
        .color_order(ColorOrder::Bgr)
        .invert_colors(mipidsi::options::ColorInversion::Inverted)
        .display_offset(40, 52) // esp_lcd_panel_set_gap(40, 52) в оригіналі
        .init(&mut Ets)
        .map_err(|_| anyhow::anyhow!("display init failed"))?;

    Ok(display)
}

/// Виводить framebuffer (native RGB565, MAP_DISPLAY_W*MAP_DISPLAY_H пікселів)
/// на екран одним блоком. `ImageRawBE` сама розкладає кожен u16 у big-endian
/// байти, тому ручний swap16(), як у C-версії, тут не потрібен.
pub fn blit(display: &mut LcdDisplay<'_>, fb: &[u16]) -> anyhow::Result<()> {
    let bytes: &[u8] = bytemuck::cast_slice(fb);
    let raw: ImageRawBE<Rgb565> = ImageRawBE::new(bytes, MAP_DISPLAY_W as u32);
    embedded_graphics::image::Image::new(&raw, Point::zero())
        .draw(display)
        .map_err(|_| anyhow::anyhow!("blit failed"))?;
    Ok(())
}
