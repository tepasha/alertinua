/*
 * ESP-IDF Unity-тести PI-регулятора яскравості (brightness_ctrl_step) -
 * та сама чиста функція, покрита докладніше й без апаратури в
 * test_host/test_brightness_ctrl.c. Тут - підтвердження, що вона й під
 * реальним ESP-IDF/toolchain (той самий float, той самий компілятор
 * xtensa-esp32-elf) поводиться так само.
 */
#include <math.h>
#include "unity.h"
#include "brightness_ctrl.h"

#define MIN_P 15.0f
#define MAX_P 100.0f
#define PERIOD_S 0.5f

TEST_CASE("яскравість зростає при яскравому світлі", "[brightness]") {
    float integral = 0.0f;
    float next = brightness_ctrl_step(1.0f, MIN_P, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_TRUE(next > MIN_P);
    TEST_ASSERT_TRUE(next <= MAX_P);
}

TEST_CASE("яскравість спадає в темряві", "[brightness]") {
    float integral = 0.0f;
    float next = brightness_ctrl_step(0.0f, MAX_P, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_TRUE(next < MAX_P);
    TEST_ASSERT_TRUE(next >= MIN_P);
}

TEST_CASE("на setpoint з нульовим інтегратором - без перерегулювання", "[brightness]") {
    float integral = 0.0f;
    float setpoint = MIN_P + 0.5f * (MAX_P - MIN_P);
    float next = brightness_ctrl_step(0.5f, setpoint, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, setpoint, next);
}

TEST_CASE("насичення: багато циклів яскравого світла не перевищує MAX", "[brightness]") {
    float integral = 0.0f;
    float current = MIN_P;
    for (int i = 0; i < 200; i++) {
        current = brightness_ctrl_step(1.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
        TEST_ASSERT_TRUE(current <= MAX_P + 0.0001f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, MAX_P, current);
}

TEST_CASE("anti-windup тримає інтегратор у межах ліміту", "[brightness]") {
    float integral = 0.0f;
    float current = MIN_P;
    for (int i = 0; i < 500; i++) {
        current = brightness_ctrl_step(1.0f, current, &integral, MIN_P, MAX_P, PERIOD_S);
        TEST_ASSERT_TRUE(fabsf(integral) <= 50.0001f);
    }
}

TEST_CASE("недоступний сенсор (lux<0) не змінює значення", "[brightness]") {
    float integral = 12.34f;
    float next = brightness_ctrl_step(-1.0f, 42.0f, &integral, MIN_P, MAX_P, PERIOD_S);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, next);
    TEST_ASSERT_EQUAL_FLOAT(12.34f, integral);
}
