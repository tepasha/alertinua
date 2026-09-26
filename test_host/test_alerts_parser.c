/*
 * Юніт-тести components/scraping/alerts_parser.c - справжній файл прошивки,
 * скомпільований тут БЕЗ ESP-IDF (заглушка esp_log.h + системний cJSON,
 * той самий upstream API, що й ESP-IDF-компонент espressif/cjson). Дивись
 * docs/TESTING.md, як зібрати й прогнати (make -C test_host).
 *
 * Формат відповіді IoT-ендпоінта alerts.in.ua
 * (/v1/iot/active_air_raid_alerts_by_oblast.json): JSON-рядок із 27
 * символів A/P/N у фіксованому порядку локацій.
 *
 * Покриття:
 *  - тривога у вибраній області / деінде / без тривог / часткова
 *  - міста "м. Київ" / "м. Севастополь" -> часткова у своїй області
 *  - повна тривога області має пріоритет над частковою від міста
 *  - зіпсований JSON, не рядок, неправильна довжина, невідомий символ
 *  - *out не змінюється при помилці
 *  - усі області одночасно (немає переповнення/сміття)
 *  - alerts_location_name(): межі індексу та назви для веб-сторінки
 */
#include <string.h>
#include "unity.h"
#include "alerts_parser.h"
#include "ukraine_map_data.h"

#define SELECTED "Київська область"

/* Позиції в рядку статусів (порядок API, див. alerts_parser.c):
 * 0 - Крим, 9 - м. Київ, 10 - Київська, 15 - Одеська, 18 - м. Севастополь,
 * 21 - Харківська. */

void setUp(void) {}
void tearDown(void) {}

/* Будує JSON-рядок "NNN...N" (27 символів) із заданими статусами на позиціях. */
static const char *make_json(const char *overrides /* пари "позиція:символ" через кому, або NULL */) {
    static char json[64];
    char statuses[ALERTS_API_LOCATIONS + 1];
    memset(statuses, 'N', ALERTS_API_LOCATIONS);
    statuses[ALERTS_API_LOCATIONS] = '\0';
    if (overrides != NULL) {
        const char *p = overrides;
        while (*p) {
            int pos = 0;
            while (*p >= '0' && *p <= '9') pos = pos * 10 + (*p++ - '0');
            p++; // ':'
            statuses[pos] = *p++;
            if (*p == ',') p++;
        }
    }
    snprintf(json, sizeof(json), "\"%s\"", statuses);
    return json;
}

static int region_index(const char *name) {
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        if (strcmp(map_regions[i].name, name) == 0) return i;
    }
    TEST_FAIL_MESSAGE("тестова назва області відсутня в ukraine_map_data.h");
    return -1;
}

static bool list_has(const int *list, int count, const char *name) {
    int idx = region_index(name);
    for (int i = 0; i < count; i++) {
        if (list[i] == idx) return true;
    }
    return false;
}

/* --- 1. тривога саме у вибраній області --- */
void test_alarm_in_selected_oblast(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("10:A"), SELECTED, &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(0, out.partial_region_count);
    TEST_ASSERT_TRUE(list_has(out.active_region_indices, out.active_region_count, "Київська область"));
}

/* --- 2. тривога в ІНШІЙ області: не "у мене", але має бути на мапі --- */
void test_alarm_in_other_oblast_not_selected(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("15:A"), SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
    TEST_ASSERT_TRUE(list_has(out.active_region_indices, out.active_region_count, "Одеська область"));
}

/* --- 3. немає жодної тривоги --- */
void test_no_alarms(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json(NULL), SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(0, out.partial_region_count);
}

/* --- 4. часткова тривога: на мапі (окремим списком), але без сирени --- */
void test_partial_alarm(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("10:P,21:P"), SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(2, out.partial_region_count);
    TEST_ASSERT_TRUE(list_has(out.partial_region_indices, out.partial_region_count, "Київська область"));
    TEST_ASSERT_TRUE(list_has(out.partial_region_indices, out.partial_region_count, "Харківська область"));
}

/* --- 5. тривога в м. Київ -> часткова в Київській області, без сирени для області --- */
void test_kyiv_city_maps_to_partial_oblast(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("9:A"), SELECTED, &out));
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(1, out.partial_region_count);
    TEST_ASSERT_TRUE(list_has(out.partial_region_indices, out.partial_region_count, "Київська область"));
}

/* --- 6. вибрано саме "м. Київ" - сирена за статусом міста --- */
void test_selected_kyiv_city(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("9:A"), "м. Київ", &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
}

/* --- 7. повна тривога області переважає часткову від міста в ній --- */
void test_full_oblast_overrides_city(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("9:A,10:A,18:A,0:A"), SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(2, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(0, out.partial_region_count);
    TEST_ASSERT_TRUE(list_has(out.active_region_indices, out.active_region_count, "Київська область"));
    TEST_ASSERT_TRUE(list_has(out.active_region_indices, out.active_region_count, "Автономна Республіка Крим"));
}

/* --- 8. м. Севастополь -> часткова в Криму --- */
void test_sevastopol_maps_to_crimea(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("18:A"), SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(1, out.partial_region_count);
    TEST_ASSERT_TRUE(list_has(out.partial_region_indices, out.partial_region_count, "Автономна Республіка Крим"));
}

/* --- 9. зіпсований JSON: false і *out не змінюється --- */
void test_malformed_json_returns_false(void) {
    alerts_result_t out;
    memset(&out, 0xAB, sizeof(out));
    TEST_ASSERT_FALSE(alerts_parse("\"NNNN", SELECTED, &out));
    unsigned char *raw = (unsigned char *)&out;
    for (size_t i = 0; i < sizeof(out); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, raw[i]);
    }
}

/* --- 10. валідний JSON, але не рядок (напр. об'єкт помилки від сервера) --- */
void test_not_a_string_returns_false(void) {
    alerts_result_t out;
    memset(&out, 0xCD, sizeof(out));
    TEST_ASSERT_FALSE(alerts_parse("{\"message\":\"API token required\"}", SELECTED, &out));
    unsigned char *raw = (unsigned char *)&out;
    TEST_ASSERT_EQUAL_HEX8(0xCD, raw[0]);
}

/* --- 11. неправильна довжина рядка --- */
void test_wrong_length_returns_false(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse("\"NNNNNNNNNNNNNNNNNNNNNNNNNN\"", SELECTED, &out));   // 26
    TEST_ASSERT_FALSE(alerts_parse("\"NNNNNNNNNNNNNNNNNNNNNNNNNNNN\"", SELECTED, &out)); // 28
    TEST_ASSERT_FALSE(alerts_parse("\"\"", SELECTED, &out));
}

/* --- 12. невідомий символ статусу --- */
void test_unknown_status_char_returns_false(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse(make_json("5:X"), SELECTED, &out));
    TEST_ASSERT_FALSE(alerts_parse(make_json("5:a"), SELECTED, &out));
}

/* --- 13. пробіли/перенос рядка навколо JSON не заважають --- */
void test_whitespace_around_json(void) {
    char json[64];
    snprintf(json, sizeof(json), "  %s\n", make_json("15:A"));
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

/* --- 14. усі локації в тривозі - кожна з 25 областей рівно один раз --- */
void test_all_locations_at_once(void) {
    char json[64];
    char statuses[ALERTS_API_LOCATIONS + 1];
    memset(statuses, 'A', ALERTS_API_LOCATIONS);
    statuses[ALERTS_API_LOCATIONS] = '\0';
    snprintf(json, sizeof(json), "\"%s\"", statuses);

    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(json, SELECTED, &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(MAP_NUM_REGIONS, out.active_region_count);
    TEST_ASSERT_EQUAL_INT(0, out.partial_region_count);
    for (int i = 0; i < out.active_region_count; i++) {
        TEST_ASSERT_TRUE(out.active_region_indices[i] >= 0 && out.active_region_indices[i] < MAP_NUM_REGIONS);
        for (int j = i + 1; j < out.active_region_count; j++) {
            TEST_ASSERT_NOT_EQUAL(out.active_region_indices[i], out.active_region_indices[j]);
        }
    }
}

/* --- 15. NULL-аргументи не падають --- */
void test_null_args_return_false(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse(NULL, SELECTED, &out));
    TEST_ASSERT_FALSE(alerts_parse(make_json(NULL), SELECTED, NULL));
}

/* --- 16. список локацій для веб-сторінки: межі та збіг з мапою --- */
void test_location_names(void) {
    TEST_ASSERT_NULL(alerts_location_name(-1));
    TEST_ASSERT_NULL(alerts_location_name(ALERTS_API_LOCATIONS));
    TEST_ASSERT_EQUAL_STRING("Автономна Республіка Крим", alerts_location_name(0));
    TEST_ASSERT_EQUAL_STRING("м. Київ", alerts_location_name(9));
    TEST_ASSERT_EQUAL_STRING("Київська область", alerts_location_name(10));
    TEST_ASSERT_EQUAL_STRING("Чернігівська область", alerts_location_name(26));
    // вибрана через веб назва з цього списку має спрацьовувати як selected
    alerts_result_t out = { 0 };
    TEST_ASSERT_TRUE(alerts_parse(make_json("15:A"), alerts_location_name(15), &out));
    TEST_ASSERT_TRUE(out.alarm_active_selected);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_in_selected_oblast);
    RUN_TEST(test_alarm_in_other_oblast_not_selected);
    RUN_TEST(test_no_alarms);
    RUN_TEST(test_partial_alarm);
    RUN_TEST(test_kyiv_city_maps_to_partial_oblast);
    RUN_TEST(test_selected_kyiv_city);
    RUN_TEST(test_full_oblast_overrides_city);
    RUN_TEST(test_sevastopol_maps_to_crimea);
    RUN_TEST(test_malformed_json_returns_false);
    RUN_TEST(test_not_a_string_returns_false);
    RUN_TEST(test_wrong_length_returns_false);
    RUN_TEST(test_unknown_status_char_returns_false);
    RUN_TEST(test_whitespace_around_json);
    RUN_TEST(test_all_locations_at_once);
    RUN_TEST(test_null_args_return_false);
    RUN_TEST(test_location_names);
    return UNITY_END();
}
