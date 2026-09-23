/*
 * Юніт-тести components/scraping/alerts_parser.c - справжній файл прошивки,
 * скомпільований тут БЕЗ ESP-IDF (заглушка esp_log.h + системний cJSON,
 * той самий upstream API, що й ESP-IDF-компонент "json"). Дивись
 * docs/TESTING.md, як зібрати й прогнати (make -C test_host).
 *
 * Покриття:
 *  - валідний JSON із тривогою у вибраній області / деінде / без тривог
 *  - зіпсований JSON, відсутнє/невірного типу поле "alerts"
 *  - фільтрація за рівнем (лише "oblast", не "district"/"hromada")
 *  - фільтрація завершених тривог (finished_at) - як явний timestamp,
 *    так і явний null, так і відсутнє поле
 *  - невідома назва області (немає в ukraine_map_data.h) ігнорується
 *  - дублікати тієї самої області в масиві не подвоюються
 *  - *out не змінюється при помилці парсингу
 *  - усі 25 реальних областей одночасно (немає переповнення/сміття)
 */
#include <string.h>
#include "unity.h"
#include "alerts_parser.h"
#include "ukraine_map_data.h"

#define SELECTED "Київська область"

void setUp(void) {}
void tearDown(void) {}

static bool result_has_region(const alerts_result_t *r, const char *name) {
    int idx = -1;
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        if (strcmp(map_regions[i].name, name) == 0) { idx = i; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(idx >= 0, "тестова назва області відсутня в ukraine_map_data.h");
    for (int i = 0; i < r->active_region_count; i++) {
        if (r->active_region_indices[i] == idx) return true;
    }
    return false;
}

/* --- 1. тривога саме у вибраній області --- */
void test_alarm_in_selected_oblast(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Київська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
    TEST_ASSERT_TRUE(result_has_region(&out, "Київська область"));
}

/* --- 2. тривога в ІНШІЙ області: не "у мене", але має бути на мапі --- */
void test_alarm_in_other_oblast_not_selected(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Одеська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
    TEST_ASSERT_TRUE(result_has_region(&out, "Одеська область"));
}

/* --- 3. немає жодної активної тривоги --- */
void test_no_alarms(void) {
    const char *json = "{\"alerts\":[]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

/* --- 4. зіпсований JSON --- */
void test_malformed_json_returns_false(void) {
    const char *json = "{\"alerts\": [ this is not json ";
    alerts_result_t out;
    memset(&out, 0xAB, sizeof(out)); // сентинел - переконатись, що НЕ зміниться
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_FALSE(ok);
    unsigned char *raw = (unsigned char *)&out;
    for (size_t i = 0; i < sizeof(out); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, raw[i]);
    }
}

/* --- 5. валідний JSON, але немає поля "alerts" --- */
void test_missing_alerts_field_returns_false(void) {
    const char *json = "{\"meta\":{\"status\":\"ok\"}}";
    alerts_result_t out;
    memset(&out, 0xCD, sizeof(out));
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_FALSE(ok);
    unsigned char *raw = (unsigned char *)&out;
    TEST_ASSERT_EQUAL_HEX8(0xCD, raw[0]); // *out не торкнулись
}

/* --- 6. "alerts" є, але не масив --- */
void test_alerts_not_array_returns_false(void) {
    const char *json = "{\"alerts\": \"несподівано рядок\"}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_FALSE(ok);
}

/* --- 7. тривога рівня "district"/"hromada" НЕ рахується як обласна --- */
void test_district_level_alert_filtered_out(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Одеська область\","
        "\"location_type\":\"district\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_FALSE(out.alarm_active_selected);
}

/* --- 8. тривога із заповненим finished_at (уже завершена) - ігнорується --- */
void test_finished_alert_with_timestamp_filtered_out(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Київська область\","
        "\"location_type\":\"oblast\",\"finished_at\":\"2026-09-23T10:00:00Z\"}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

/* --- 9. поле finished_at взагалі ВІДСУТНЄ в об'єкті - все ще активна --- */
void test_missing_finished_at_field_is_active(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Київська область\","
        "\"location_type\":\"oblast\"}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

/* --- 10. невідома назва області - пропускається, без падіння --- */
void test_unknown_oblast_name_ignored(void) {
    const char *json =
        "{\"alerts\":[{\"location_oblast\":\"Марсіанська область\","
        "\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
    TEST_ASSERT_FALSE(out.alarm_active_selected);
}

/* --- 11. запис без "location_oblast" (несподіваний/неповний) - пропускається --- */
void test_missing_location_oblast_field_ignored(void) {
    const char *json =
        "{\"alerts\":[{\"location_type\":\"oblast\",\"finished_at\":null}]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(0, out.active_region_count);
}

/* --- 12. дублікат тієї самої області в масиві - рахується один раз --- */
void test_duplicate_region_deduped(void) {
    const char *json =
        "{\"alerts\":["
        "{\"location_oblast\":\"Одеська область\",\"location_type\":\"oblast\",\"finished_at\":null},"
        "{\"location_oblast\":\"Одеська область\",\"location_type\":\"oblast\",\"finished_at\":null}"
        "]}";
    alerts_result_t out = { 0 };
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(1, out.active_region_count);
}

/* --- 13. усі 25 реальних областей одночасно - немає переповнення/сміття --- */
void test_all_real_regions_at_once(void) {
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
    bool ok = alerts_parse(json, SELECTED, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(out.alarm_active_selected);
    TEST_ASSERT_EQUAL_INT(MAP_NUM_REGIONS, out.active_region_count);
    // NOTE: MAP_NUM_REGIONS (25) < ALERTS_MAX_ACTIVE_REGIONS (27), тож захист
    // від переповнення (active_region_count < ALERTS_MAX_ACTIVE_REGIONS)
    // наразі є "мертвим кодом" - його не можна реалістично досягти, доки в
    // ukraine_map_data.h лишається 25 областей. Лишаємо задокументованим тут,
    // а не імітуємо штучний масив на >27 різних назв, яких карта не знає.
}

/* --- 14. NULL-аргументи не падають --- */
void test_null_args_return_false(void) {
    alerts_result_t out = { 0 };
    TEST_ASSERT_FALSE(alerts_parse(NULL, SELECTED, &out));
    TEST_ASSERT_FALSE(alerts_parse("{}", SELECTED, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_in_selected_oblast);
    RUN_TEST(test_alarm_in_other_oblast_not_selected);
    RUN_TEST(test_no_alarms);
    RUN_TEST(test_malformed_json_returns_false);
    RUN_TEST(test_missing_alerts_field_returns_false);
    RUN_TEST(test_alerts_not_array_returns_false);
    RUN_TEST(test_district_level_alert_filtered_out);
    RUN_TEST(test_finished_alert_with_timestamp_filtered_out);
    RUN_TEST(test_missing_finished_at_field_is_active);
    RUN_TEST(test_unknown_oblast_name_ignored);
    RUN_TEST(test_missing_location_oblast_field_ignored);
    RUN_TEST(test_duplicate_region_deduped);
    RUN_TEST(test_all_real_regions_at_once);
    RUN_TEST(test_null_args_return_false);
    return UNITY_END();
}
