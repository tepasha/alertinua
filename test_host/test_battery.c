/*
 * Юніт-тести components/battery/battery_percent.c - перерахунок напруги
 * Li-Pol у відсотки заряду. Файл без залежностей від ESP-IDF, тож
 * компілюється й запускається звичайним gcc (make -C test_host).
 */
#include "unity.h"
#include "battery_percent.h"

void setUp(void) {}
void tearDown(void) {}

/* --- 1. Межі кривої: повна / розряджена --- */
void test_full_and_empty(void) {
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4200));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3300));
}

/* --- 2. Поза кривою - насичення, без виходу за 0..100 --- */
void test_saturation_outside_curve(void) {
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(4350));
    TEST_ASSERT_EQUAL_INT(100, battery_percent_from_mv(5000));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(3000));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(0));
    TEST_ASSERT_EQUAL_INT(0, battery_percent_from_mv(-100));
}

/* --- 3. Точно в точках кривої --- */
void test_curve_points(void) {
    TEST_ASSERT_EQUAL_INT(80, battery_percent_from_mv(4020));
    TEST_ASSERT_EQUAL_INT(50, battery_percent_from_mv(3840));
    TEST_ASSERT_EQUAL_INT(10, battery_percent_from_mv(3690));
}

/* --- 4. Між точками - лінійна інтерполяція --- */
void test_interpolation(void) {
    // 4050 - посередині між 4020 (80%) і 4080 (85%)
    int p = battery_percent_from_mv(4050);
    TEST_ASSERT_TRUE(p >= 82 && p <= 83);
    // 3455 - посередині між 3300 (0%) і 3610 (5%)
    p = battery_percent_from_mv(3455);
    TEST_ASSERT_TRUE(p >= 2 && p <= 3);
}

/* --- 5. Монотонність: більша напруга ніколи не дає менший заряд --- */
void test_monotonic(void) {
    int prev = battery_percent_from_mv(3000);
    for (int mv = 3000; mv <= 4400; mv += 5) {
        int p = battery_percent_from_mv(mv);
        TEST_ASSERT_TRUE_MESSAGE(p >= prev, "заряд не має зменшуватись із ростом напруги");
        TEST_ASSERT_TRUE(p >= 0 && p <= 100);
        prev = p;
    }
}

/* --- 6. Живлення від USB визначається за порогом --- */
void test_external_power(void) {
    TEST_ASSERT_FALSE(battery_is_external_power(4200));
    TEST_ASSERT_FALSE(battery_is_external_power(BATTERY_EXTERNAL_POWER_MV));
    TEST_ASSERT_TRUE(battery_is_external_power(BATTERY_EXTERNAL_POWER_MV + 1));
    TEST_ASSERT_TRUE(battery_is_external_power(5000));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_and_empty);
    RUN_TEST(test_saturation_outside_curve);
    RUN_TEST(test_curve_points);
    RUN_TEST(test_interpolation);
    RUN_TEST(test_monotonic);
    RUN_TEST(test_external_power);
    return UNITY_END();
}
