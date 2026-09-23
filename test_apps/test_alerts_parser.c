/*
 * ESP-IDF Unity-тести components/scraping/alerts_parser.c - реальна збірка
 * з реальним ESP-IDF-компонентом "json" (cJSON). Той самий модуль
 * докладніше й без апаратури покритий у test_host/test_alerts_parser.c
 * (14 сценаріїв, прогнані звичайним gcc) - тут представлена основна
 * підмножина, щоб підтвердити, що логіка так само коректно працює під
 * справжнім ESP-IDF/toolchain користувача, а не лише на хості.
 */
#include <string.h>
#include "unity.h"
#include "alerts_parser.h"
#include "ukraine_map_data.h"

#define SELECTED "Київська область"

TEST_CASE("тривога у вибраній області розпізнається", "[alerts]") {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Київська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

TEST_CASE("тривога в іншій області не позначається як 'моя', але на мапі є", "[alerts]") {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Одеська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

TEST_CASE("зіпсований JSON повертає false", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("{\"alerts\": [ oops ", SELECTED, &out));
}

TEST_CASE("відсутнє поле alerts повертає false", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("{\"meta\":{}}", SELECTED, &out));
}

TEST_CASE("тривога рівня district ігнорується (не oblast)", "[alerts]") {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Одеська область\","
        "\"location_type\":\"district\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

TEST_CASE("завершена тривога (finished_at заповнене) ігнорується", "[alerts]") {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Київська область\","
        "\"location_type\":\"oblast\",\"finished_at\":\"2026-09-23T10:00:00Z\"}]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

TEST_CASE("невідома назва області ігнорується без падіння", "[alerts]") {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Марсіанська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

TEST_CASE("дублікат тієї самої області рахується один раз", "[alerts]") {
    const char *json =
        "{\"alerts\":["
        "{\"location_oblast\":\"Одеська область\",\"location_type\":\"oblast\",\"finished_at\":null},"
        "{\"location_oblast\":\"Одеська область\",\"location_type\":\"oblast\",\"finished_at\":null}"
        "]}";
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

TEST_CASE("усі реальні області одночасно - без переповнення", "[alerts]") {
    char json[4096];
    strcpy(json, "{\"alerts\":[");
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        char item[160];
        snprintf(item, sizeof(item),
                 "%s{\"location_oblast\":\"%s\",\"location_type\":\"oblast\",\"finished_at\":null}",
                 (i > 0) ? "," : "", map_regions[i].name);
        strcat(json, item);
    }
    strcat(json, "]}");

    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(MAP_NUM_REGIONS, out.active_region_count);
}

TEST_CASE("NULL-аргументи не падають", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse(NULL, SELECTED, &out));
    TEST_ASSERT_FALSE(alerts_parse("{}", SELECTED, NULL));
}
