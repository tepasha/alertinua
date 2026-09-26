#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "alerts_parser.h"
#include "ukraine_map_data.h" // map_regions[], MAP_NUM_REGIONS

static const char *TAG = "ALERTS_PARSER";

/* Порядок локацій у рядку статусів IoT-ендпоінта - такий самий, як в
 * офіційному клієнті alerts.in.ua (alerts_in_ua/air_raid_alert_oblast_statuses.py). */
static const char *const API_LOCATIONS[ALERTS_API_LOCATIONS] = {
    "Автономна Республіка Крим",
    "Волинська область",
    "Вінницька область",
    "Дніпропетровська область",
    "Донецька область",
    "Житомирська область",
    "Закарпатська область",
    "Запорізька область",
    "Івано-Франківська область",
    "м. Київ",
    "Київська область",
    "Кіровоградська область",
    "Луганська область",
    "Львівська область",
    "Миколаївська область",
    "Одеська область",
    "Полтавська область",
    "Рівненська область",
    "м. Севастополь",
    "Сумська область",
    "Тернопільська область",
    "Харківська область",
    "Херсонська область",
    "Хмельницька область",
    "Черкаська область",
    "Чернівецька область",
    "Чернігівська область",
};

const char *alerts_location_name(int index) {
    if (index < 0 || index >= ALERTS_API_LOCATIONS) {
        return NULL;
    }
    return API_LOCATIONS[index];
}

typedef enum {
    REGION_NO_ALERT = 0,
    REGION_PARTIAL  = 1,
    REGION_FULL     = 2,
} region_alert_t;

static int find_region_index(const char *name) {
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        if (strcmp(map_regions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Полігон мапи, на якому показуємо локацію API. Міста без власного полігона
 * прив'язані до області навколо них. */
static int map_index_for_location(const char *location, bool *is_city) {
    *is_city = false;
    if (strcmp(location, "м. Київ") == 0) {
        *is_city = true;
        return find_region_index("Київська область");
    }
    if (strcmp(location, "м. Севастополь") == 0) {
        *is_city = true;
        return find_region_index("Автономна Республіка Крим");
    }
    return find_region_index(location);
}

bool alerts_parse(const char *json_str, const char *selected_oblast_name, alerts_result_t *out) {
    if (json_str == NULL || out == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "не вдалось розпарсити JSON (не валідний або обірваний)");
        return false;
    }
    if (!cJSON_IsString(root) || root->valuestring == NULL) {
        ESP_LOGE(TAG, "очікувався JSON-рядок статусів областей");
        cJSON_Delete(root);
        return false;
    }

    const char *statuses = root->valuestring;
    if (strlen(statuses) != ALERTS_API_LOCATIONS) {
        ESP_LOGE(TAG, "рядок статусів має довжину %u, очікувалось %d",
                 (unsigned)strlen(statuses), ALERTS_API_LOCATIONS);
        cJSON_Delete(root);
        return false;
    }

    region_alert_t per_region[MAP_NUM_REGIONS] = { REGION_NO_ALERT };
    bool alarm_selected = false;

    for (int i = 0; i < ALERTS_API_LOCATIONS; i++) {
        region_alert_t level;
        switch (statuses[i]) {
            case 'A': level = REGION_FULL; break;
            case 'P': level = REGION_PARTIAL; break;
            case 'N': level = REGION_NO_ALERT; break;
            default:
                ESP_LOGE(TAG, "невідомий статус '%c' на позиції %d", statuses[i], i);
                cJSON_Delete(root);
                return false;
        }

        if (selected_oblast_name != NULL && level == REGION_FULL &&
            strcmp(API_LOCATIONS[i], selected_oblast_name) == 0) {
            alarm_selected = true;
        }

        bool is_city = false;
        int idx = map_index_for_location(API_LOCATIONS[i], &is_city);
        if (idx < 0) {
            continue; // немає полігона на мапі - пропускаємо, це не помилка
        }
        if (is_city && level == REGION_FULL) {
            level = REGION_PARTIAL; // тривога в місті - це лише частина області навколо
        }
        if (level > per_region[idx]) {
            per_region[idx] = level;
        }
    }

    cJSON_Delete(root);

    alerts_result_t result = { 0 };
    result.alarm_active_selected = alarm_selected;
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        if (per_region[i] == REGION_FULL && result.active_region_count < ALERTS_MAX_ACTIVE_REGIONS) {
            result.active_region_indices[result.active_region_count++] = i;
        } else if (per_region[i] == REGION_PARTIAL && result.partial_region_count < ALERTS_MAX_ACTIVE_REGIONS) {
            result.partial_region_indices[result.partial_region_count++] = i;
        }
    }

    *out = result;
    return true;
}
