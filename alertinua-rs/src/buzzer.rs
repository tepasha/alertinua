//! П'єзо-зумер через LEDC PWM, портовано з components/buzzer/buzzer.c.

use esp_idf_hal::gpio::Gpio25;
use esp_idf_hal::ledc::{config::TimerConfig, LedcChannel, LedcDriver, LedcTimerDriver};
use esp_idf_hal::peripheral::Peripheral;
use esp_idf_hal::prelude::*;
use esp_idf_svc::hal::delay::FreeRtos;

const BUZZER_DUTY_50PCT_RES_BITS: u32 = 10; // LEDC_TIMER_10_BIT, шпаруватість 0..1023
const BUZZER_DUTY_50PCT: u32 = 512; // 50% - найгучніше й найчистіше для п'єзо
const START_FREQ_HZ: u32 = 2000;

pub struct Buzzer<'d> {
    driver: LedcDriver<'d>,
}

impl<'d> Buzzer<'d> {
    /// Аналог `buzzer_init()`. GPIO25 - `BUZZER_GPIO` з оригіналу.
    pub fn new(
        timer: impl Peripheral<P = impl esp_idf_hal::ledc::LedcTimer> + 'd,
        channel: impl Peripheral<P = impl LedcChannel> + 'd,
        pin: Gpio25,
    ) -> anyhow::Result<Self> {
        let timer_config = TimerConfig::new()
            .frequency(START_FREQ_HZ.Hz())
            .resolution(esp_idf_hal::ledc::Resolution::Bits10);
        let timer_driver = LedcTimerDriver::new(timer, &timer_config)?;
        let driver = LedcDriver::new(channel, timer_driver, pin)?;
        Ok(Self { driver })
    }

    /// Аналог `buzzer_tone()`.
    pub fn tone(&mut self, freq_hz: u32, duration_ms: u32) -> anyhow::Result<()> {
        if freq_hz == 0 {
            FreeRtos::delay_ms(duration_ms);
            return Ok(());
        }

        self.driver.set_frequency(freq_hz.Hz())?;
        self.driver.set_duty(BUZZER_DUTY_50PCT)?;
        FreeRtos::delay_ms(duration_ms);
        self.off()?;
        Ok(())
    }

    /// Аналог `buzzer_off()`.
    pub fn off(&mut self) -> anyhow::Result<()> {
        self.driver.set_duty(0)?;
        Ok(())
    }

    /// Аналог `buzzer_play_siren()`: висхідно-низхідна сирена, 100 циклів.
    pub fn play_siren(&mut self) -> anyhow::Result<()> {
        const FREQ_LOW: u32 = 400;
        const FREQ_HIGH: u32 = 1200;
        const STEP_MS: u32 = 50;
        const CYCLES: u32 = 100;
        const STEP_HZ: u32 = 20;

        self.driver.set_duty(BUZZER_DUTY_50PCT)?;

        for _ in 0..CYCLES {
            let mut f = FREQ_LOW;
            while f < FREQ_HIGH {
                self.driver.set_frequency(f.Hz())?;
                FreeRtos::delay_ms(STEP_MS);
                f += STEP_HZ;
            }
            let mut f = FREQ_HIGH;
            while f > FREQ_LOW {
                self.driver.set_frequency(f.Hz())?;
                FreeRtos::delay_ms(STEP_MS);
                f -= STEP_HZ;
            }
        }

        self.off()
    }
}

const _: u32 = BUZZER_DUTY_50PCT_RES_BITS; // документує розрядність, використану в TimerConfig вище
