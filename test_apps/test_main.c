/*
 * Точка входу тестового застосунку ESP-IDF Unity. Це ОКРЕМИЙ проєкт
 * (test_apps/unit_tests/), не входить у прошивку - збирається й прошивається
 * так само, як main-проєкт, але з цієї директорії:
 *
 *   cd test_apps/unit_tests
 *   idf.py set-target esp32
 *   idf.py -p /dev/ttyUSB0 flash monitor
 *
 * Після прошивки в моніторі відкриється інтерактивне меню Unity: натисніть
 * "*" + Enter, щоб прогнати всі тести, або назву тегу (напр. "[alerts]"),
 * щоб прогнати лише один набір. Детальніше - docs/TESTING.md.
 */
#include "unity.h"
#include "app_state.h"

void app_main(void) {
    // app_state - глобальний спільний стан з мьютексом; ініціалізується
    // один раз тут (як і в main/main.c), щоб тести test_app_state.c могли
    // безпечно писати/читати його через мьютекс.
    app_state_init();

    unity_run_menu();
}
