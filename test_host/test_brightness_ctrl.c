/*
 * Юніт-тести PI-регулятора яскравості
 * (components/brightness_ctrl/brightness_ctrl_step.c) - файл без жодної
 * залежності від ESP-IDF/FreeRTOS, тож компілюється й запускається тут
 * звичайним gcc (make -C test_host), без апаратури. Параметри KP/KI/лімітів
 * anti-windup узгоджені з реальними значеннями в brightness_ctrl_step.c;
 * тут вони НЕ дублюються як магічні числа - тести перевіряють поведінку
 * (напрямок реакції, збіжність, межі), а не відтворюють формулу вручну,
 * щоб тест не "підганявся" під реалізацію.
 */
#include <math.h>
#include "unity.h"
#include "brightness_ctrl.h"

/* Ті самі значення за замовчуванням, що й у Kconfig (main/Kconfig.projbuild). */
#define MIN_P 15.0f
#define MAX_P 100.0f
#define PERIOD_S 0.5f

void setUp(void) {}
void tearDown(void) {}

/* --- 1. Яскравість реагує в правильному НАПРЯМКУ на світло --- */
void test_step_increases_toward_bright_setpoint(void) {
    float integral = 0.0f;
    float current = MIN_P; // темно, підсвітка мінімальна
    float next = brightness_ctrl_step(1.0f /* максимум світла */, current, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_TRUE_MESSAGE(next > current, "при яскравому світлі яскравість підсвітки має зростати");
    TEST_ASSERT_TRUE(next <= MAX_P);
}

void test_step_decreases_toward_dark_setpoint(void) {
    float integral = 0.0f;
    float current = MAX_P; // світло, підсвітка максимальна
    float next = brightness_ctrl_step(0.0f /* темрява */, current, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_TRUE_MESSAGE(next < current, "у темряві яскравість підсвітки має спадати");
    TEST_ASSERT_TRUE(next >= MIN_P);
}

/* --- 2. При error==0 (вже на setpoint, інтегратор=0) регулятор не "тремтить" --- */
void test_step_stable_at_setpoint_no_overshoot(void) {
    float integral = 0.0f;
    float lux = 0.5f;
    float setpoint = MIN_P + lux * (MAX_P - MIN_P); // 57.5
    float next = brightness_ctrl_step(lux, setpoint, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, setpoint, next);
}

/* --- 3. Насичення виходу: багато циклів яскравого світла ніколи не
 *        перевищує MAX_P, і зрештою збігається до нього --- */
void test_step_converges_and_never_exceeds_max(void) {
    float integral = 0.0f;
    float current = MIN_P;
    for (int i = 0; i < 200; i++) {
        current = brightness_ctrl_step(1.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
        TEST_ASSERT_TRUE_MESSAGE(current <= MAX_P + 0.0001f, "яскравість не має перевищувати MAX_P НА ЖОДНОМУ кроці");
        TEST_ASSERT_TRUE(current >= MIN_P - 0.0001f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, MAX_P, current);
}

/* --- 4. Те саме для темряви: збіжність до MIN_P, ніколи нижче --- */
void test_step_converges_and_never_below_min(void) {
    float integral = 0.0f;
    float current = MAX_P;
    for (int i = 0; i < 200; i++) {
        current = brightness_ctrl_step(0.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
        TEST_ASSERT_TRUE_MESSAGE(current >= MIN_P - 0.0001f, "яскравість не має опускатись нижче MIN_P НА ЖОДНОМУ кроці");
        TEST_ASSERT_TRUE(current <= MAX_P + 0.0001f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, MIN_P, current);
}

/* --- 5. Anti-windup: інтегральна складова НІКОЛИ не виходить за межі
 *        свого ліміту (±50), навіть під постійною екстремальною похибкою --- */
void test_anti_windup_bounds_integral(void) {
    float integral = 0.0f;
    float current = MIN_P;
    for (int i = 0; i < 500; i++) {
        current = brightness_ctrl_step(1.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(integral) <= 50.0f + 0.0001f,
                                  "інтегратор має бути обмежений anti-windup лімітом");
    }
}

/* --- 6. Сенсор недоступний (lux<0): яскравість і інтегратор НЕ змінюються --- */
void test_sensor_unavailable_holds_value(void) {
    float integral = 12.34f;
    float current = 42.0f;
    float next = brightness_ctrl_step(-1.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, next);
    TEST_ASSERT_EQUAL_FLOAT(12.34f, integral); // інтегратор не "поїхав" у темну сторону
}

/* --- 7. Захисне насичення діє одноразово навіть при аномальному вхідному
 *        значенні current_percent (поза [min,max]) --- */
void test_step_clamps_out_of_range_current_in_one_call(void) {
    float integral = 0.0f;
    float next = brightness_ctrl_step(0.5f, /*current=*/500.0f, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_TRUE(next <= MAX_P);
    TEST_ASSERT_TRUE(next >= MIN_P);
}

/* --- 8. Працює з довільними (не лише дефолтними) межами min/max --- */
void test_step_respects_custom_bounds(void) {
    float integral = 0.0f;
    float current = 0.0f;
    for (int i = 0; i < 100; i++) {
        current = brightness_ctrl_step(1.0f, current, &integral, 0.0f, 10.0f, PERIOD_S);
        TEST_ASSERT_TRUE(current <= 10.0f + 0.0001f);
        TEST_ASSERT_TRUE(current >= 0.0f - 0.0001f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, current);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_step_increases_toward_bright_setpoint);
    RUN_TEST(test_step_decreases_toward_dark_setpoint);
    RUN_TEST(test_step_stable_at_setpoint_no_overshoot);
    RUN_TEST(test_step_converges_and_never_exceeds_max);
    RUN_TEST(test_step_converges_and_never_below_min);
    RUN_TEST(test_anti_windup_bounds_integral);
    RUN_TEST(test_sensor_unavailable_holds_value);
    RUN_TEST(test_step_clamps_out_of_range_current_in_one_call);
    RUN_TEST(test_step_respects_custom_bounds);
    return UNITY_END();
}
