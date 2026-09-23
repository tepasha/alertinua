/*
 * ESP-IDF Unity-тести components/app_state (мьютекс-захищений спільний
 * стан). Це ЄДИНИЙ модуль з реальною конкурентністю (SemaphoreHandle_t),
 * тому на відміну від alerts_parser/brightness_ctrl його неможливо
 * протестувати на хості без FreeRTOS - лише тут, під реальним ESP-IDF.
 *
 * ПРИМІТКА щодо порядку тестів: app_state - це один глобальний стан без
 * функції "reset" (як і в реальній прошивці - app_state_init() викликається
 * рівно раз з app_main(), див. test_main.c). Тому кожен тест тут написаний
 * як самодостатній round-trip ("встановив -> одразу прочитав те саме"), а
 * НЕ як перевірка стартового/дефолтного значення - це навмисно, щоб тести
 * лишались коректними незалежно від порядку запуску через unity_run_menu().
 */
#include "unity.h"
#include "app_state.h"

TEST_CASE("set/get основного стану - round-trip", "[app_state]") {
    app_state_set(APP_STATE_ALARM);
    TEST_ASSERT_EQUAL(APP_STATE_ALARM, app_state_get());

    app_state_set(APP_STATE_CLEAR);
    TEST_ASSERT_EQUAL(APP_STATE_CLEAR, app_state_get());
}

TEST_CASE("app_state_name повертає читабельні назви", "[app_state]") {
    TEST_ASSERT_EQUAL_STRING("ALARM", app_state_name(APP_STATE_ALARM));
    TEST_ASSERT_EQUAL_STRING("CLEAR", app_state_name(APP_STATE_CLEAR));
    TEST_ASSERT_EQUAL_STRING("ERROR", app_state_name(APP_STATE_ERROR));
}

TEST_CASE("set/get Wi-Fi статусу - round-trip", "[app_state]") {
    app_state_set_wifi_status(WIFI_STATUS_CONNECTED);
    TEST_ASSERT_EQUAL(WIFI_STATUS_CONNECTED, app_state_get_wifi_status());

    app_state_set_wifi_status(WIFI_STATUS_PROVISIONING);
    TEST_ASSERT_EQUAL(WIFI_STATUS_PROVISIONING, app_state_get_wifi_status());
}

TEST_CASE("тривога: set/get масиву регіонів - round-trip", "[app_state]") {
    int regions[3] = { 5, 9, 21 };
    app_state_set_alarm(true, regions, 3);

    bool active = false;
    int out[APP_STATE_MAX_ACTIVE_REGIONS] = { 0 };
    int count = 0;
    app_state_get_alarm(&active, out, &count);

    TEST_ASSERT_TRUE(active);
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_INT(5, out[0]);
    TEST_ASSERT_EQUAL_INT(9, out[1]);
    TEST_ASSERT_EQUAL_INT(21, out[2]);
    TEST_ASSERT_TRUE(app_state_is_alarm_active());
}

TEST_CASE("тривога: count>MAX клемпиться (гранична умова)", "[app_state]") {
    int regions[APP_STATE_MAX_ACTIVE_REGIONS + 5];
    for (int i = 0; i < APP_STATE_MAX_ACTIVE_REGIONS + 5; i++) regions[i] = i;

    app_state_set_alarm(true, regions, APP_STATE_MAX_ACTIVE_REGIONS + 5);

    int count = -1;
    app_state_get_alarm(NULL, NULL, &count);
    TEST_ASSERT_EQUAL_INT(APP_STATE_MAX_ACTIVE_REGIONS, count);
}

TEST_CASE("тривога: від'ємний count клемпиться до 0", "[app_state]") {
    app_state_set_alarm(false, NULL, -5);

    int count = -1;
    app_state_get_alarm(NULL, NULL, &count);
    TEST_ASSERT_EQUAL_INT(0, count);
}

TEST_CASE("яскравість: set/get - round-trip", "[app_state]") {
    app_state_set_brightness(37);
    TEST_ASSERT_EQUAL_UINT8(37, app_state_get_brightness());
}

TEST_CASE("яскравість: насичення при >100 (гранична умова)", "[app_state]") {
    app_state_set_brightness(250);
    TEST_ASSERT_EQUAL_UINT8(100, app_state_get_brightness());
}

TEST_CASE("статистика fetch: успіх скидає лічильник невдач", "[app_state]") {
    app_state_record_fetch(1000, false);
    app_state_record_fetch(2000, false);
    app_state_record_fetch(3000, true); // успіх -> лічильник має скинутись

    int64_t last_us = 0;
    uint32_t failures = 0;
    app_state_get_fetch_stats(&last_us, &failures);

    TEST_ASSERT_EQUAL_INT64(3000, last_us);
    TEST_ASSERT_EQUAL_UINT32(0, failures);
}

TEST_CASE("статистика fetch: послідовні невдачі накопичуються", "[app_state]") {
    // Знімаємо поточне значення лічильника, щоб тест не залежав від того,
    // скільки невдач накопичилось у попередніх тестах цього файлу.
    uint32_t before = 0;
    app_state_get_fetch_stats(NULL, &before);

    app_state_record_fetch(500, false);
    app_state_record_fetch(500, false);

    uint32_t after = 0;
    app_state_get_fetch_stats(NULL, &after);
    TEST_ASSERT_EQUAL_UINT32(before + 2, after);
}
