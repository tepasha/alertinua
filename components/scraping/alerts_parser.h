#pragma once
#include <stdbool.h>

#define ALERTS_MAX_ACTIVE_REGIONS 27

/* Кількість локацій у відповіді IoT-ендпоінта (27 символів у рядку). */
#define ALERTS_API_LOCATIONS 27

typedef struct {
    bool alarm_active_selected;                         // тривога на ВСІЙ вибраній (Kconfig) області ('A')
    int  active_region_indices[ALERTS_MAX_ACTIVE_REGIONS]; // індекси в map_regions[]: тривога по всій області ('A')
    int  active_region_count;
    int  partial_region_indices[ALERTS_MAX_ACTIVE_REGIONS]; // індекси в map_regions[]: тривога в частині області ('P')
    int  partial_region_count;
} alerts_result_t;

/*
 * Парсить відповідь alerts.in.ua IoT-ендпоінта
 *   GET /v1/iot/active_air_raid_alerts_by_oblast.json
 * Це JSON-рядок рівно з 27 символів, по одному на локацію (порядок - див.
 * alerts_parser.c): 'A' - повітряна тривога по всій області,
 * 'P' - тривога лише в частині області (район/громада), 'N' - тривоги немає.
 * Приклад: "NNNNNNANNNNNNNNNNNNNPANNNNN"
 *
 * Результат:
 *  - alarm_active_selected: 'A' у локації selected_oblast_name (назва як у
 *    списку API, напр. "Київська область" або "м. Київ");
 *  - області з 'A' і з 'P' (для відображення на мапі). Міста "м. Київ" і
 *    "м. Севастополь" окремих полігонів на мапі не мають, тому тривога в
 *    них показується як часткова у Київській області / Криму.
 *
 * Повертає false при будь-якій помилці (не JSON, не рядок, неправильна
 * довжина, невідомий символ) - у такому разі *out гарантовано не
 * змінюється, а викликач має показати банер помилки, а не падати.
 */
bool alerts_parse(const char *json_str, const char *selected_oblast_name, alerts_result_t *out);

/*
 * Назва локації API за індексом 0..ALERTS_API_LOCATIONS-1 (у порядку символів
 * у рядку статусів), або NULL, якщо індекс поза межами. Використовується
 * сторінкою налаштувань для списку вибору області.
 */
const char *alerts_location_name(int index);
