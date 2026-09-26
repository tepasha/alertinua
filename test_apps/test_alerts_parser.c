/*
 * ESP-IDF Unity-тести components/scraping/alerts_parser.c - реальна збірка
 * з реальним ESP-IDF-компонентом cJSON. Той самий модуль докладніше й без
 * апаратури покритий у test_host/test_alerts_parser.c (15 сценаріїв,
 * прогнані звичайним gcc) - тут основна підмножина, щоб підтвердити, що
 * логіка так само працює під справжнім ESP-IDF/toolchain.
 *
 * Вхід - відповідь IoT-ендпоінта alerts.in.ua: JSON-рядок із 27 символів
 * A/P/N. Позиції: 9 - м. Київ, 10 - Київська область, 15 - Одеська область.
 */
#include <string.h>
#include "unity.h"
#include "alerts_parser.h"
#include "ukraine_map_data.h"

#define SELECTED "Київська область"

/*                              0123456789012345678901234567 */
#define JSON_NONE             "\"NNNNNNNNNNNNNNNNNNNNNNNNNNN\""
#define JSON_KYIV_OBLAST_A    "\"NNNNNNNNNNANNNNNNNNNNNNNNNN\""
#define JSON_ODESA_A          "\"NNNNNNNNNNNNNNNANNNNNNNNNNN\""
#define JSON_KYIV_OBLAST_P    "\"NNNNNNNNNNPNNNNNNNNNNNNNNNN\""
#define JSON_KYIV_CITY_A      "\"NNNNNNNNNANNNNNNNNNNNNNNNNN\""

TEST_CASE("тривога у вибраній області розпізнається", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(JSON_KYIV_OBLAST_A, SELECTED, &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

TEST_CASE("тривога в іншій області не позначається як 'моя', але на мапі є", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(JSON_ODESA_A, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

TEST_CASE("без тривог - порожні списки", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(JSON_NONE, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(0, out.partial_region_count);
}

TEST_CASE("часткова тривога - на мапі, але без сирени", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(JSON_KYIV_OBLAST_P, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(1, out.partial_region_count);
}

TEST_CASE("тривога в м. Київ - часткова в Київській області", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(JSON_KYIV_CITY_A, SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.partial_region_count);
}

TEST_CASE("зіпсований JSON повертає false", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("\"NNNN", SELECTED, &out));
}

TEST_CASE("відповідь-об'єкт замість рядка повертає false", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("{\"message\":\"error\"}", SELECTED, &out));
}

TEST_CASE("неправильна довжина рядка повертає false", "[alerts]") {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("\"NNN\"", SELECTED, &out));
}
