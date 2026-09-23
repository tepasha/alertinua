#pragma once
#include <stdbool.h>

#define ALERTS_MAX_ACTIVE_REGIONS 27

typedef struct {
    bool alarm_active_selected;                         // тривога у вибраній (Kconfig) області
    int  active_region_indices[ALERTS_MAX_ACTIVE_REGIONS]; // індекси в map_regions[]
    int  active_region_count;
} alerts_result_t;

/*
 * Парсить відповідь alerts.in.ua (GET /v1/alerts/active.json) і визначає:
 *  - чи активна (незавершена, finished_at == null) тривога рівня "область"
 *    у selected_oblast_name;
 *  - список усіх областей з активною тривогою (для відображення на мапі).
 *
 * Повертає false при будь-якій помилці парсингу/валідації (зіпсований JSON,
 * відсутнє очікуване поле "alerts" тощо) - у такому разі *out гарантовано не
 * змінюється, а викликач має трактувати це як тимчасову помилку даних
 * (показати банер помилки), а не падати.
 */
bool alerts_parse(const char *json_str, const char *selected_oblast_name, alerts_result_t *out);
