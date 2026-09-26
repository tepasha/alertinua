#include "battery_percent.h"

/* Типова крива розряду однобанкового Li-Pol: напруга (мВ) -> заряд (%).
 * Крива нелінійна: більшу частину ємності батарея віддає в районі 3.7-3.9 В. */
typedef struct {
    int mv;
    int percent;
} curve_point_t;

static const curve_point_t CURVE[] = {
    { 4200, 100 },
    { 4150, 95 },
    { 4110, 90 },
    { 4080, 85 },
    { 4020, 80 },
    { 3980, 75 },
    { 3950, 70 },
    { 3910, 65 },
    { 3870, 60 },
    { 3850, 55 },
    { 3840, 50 },
    { 3820, 45 },
    { 3800, 40 },
    { 3790, 35 },
    { 3770, 30 },
    { 3750, 25 },
    { 3730, 20 },
    { 3710, 15 },
    { 3690, 10 },
    { 3610, 5 },
    { 3300, 0 },
};

#define CURVE_LEN ((int)(sizeof(CURVE) / sizeof(CURVE[0])))

int battery_percent_from_mv(int voltage_mv) {
    if (voltage_mv >= CURVE[0].mv) {
        return 100;
    }
    if (voltage_mv <= CURVE[CURVE_LEN - 1].mv) {
        return 0;
    }
    for (int i = 1; i < CURVE_LEN; i++) {
        if (voltage_mv >= CURVE[i].mv) {
            const curve_point_t *hi = &CURVE[i - 1];
            const curve_point_t *lo = &CURVE[i];
            // лінійна інтерполяція між сусідніми точками, з округленням
            int num = (voltage_mv - lo->mv) * (hi->percent - lo->percent);
            int den = hi->mv - lo->mv;
            return lo->percent + (num + den / 2) / den;
        }
    }
    return 0; // недосяжно
}

bool battery_is_external_power(int voltage_mv) {
    return voltage_mv > BATTERY_EXTERNAL_POWER_MV;
}
