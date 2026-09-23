#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "alerts_parser.h"
#include "ukraine_map_data.h" // map_regions[], MAP_NUM_REGIONS

static const char *TAG = "ALERTS_PARSER";

static int find_region_index(const char *name) {
    if (name == NULL) {
        return -1;
    }
    for (int i = 0; i < MAP_NUM_REGIONS; i++) {
        if (strcmp(map_regions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
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

    cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts");
    if (!cJSON_IsArray(alerts)) {
        ESP_LOGE(TAG, "у відповіді відсутнє поле \"alerts\" або воно не масив");
        cJSON_Delete(root);
        return false;
    }

    alerts_result_t result = { 0 };

    cJSON *alert = NULL;
    cJSON_ArrayForEach(alert, alerts) {
        cJSON *location_oblast = cJSON_GetObjectItemCaseSensitive(alert, "location_oblast");
        cJSON *location_type   = cJSON_GetObjectItemCaseSensitive(alert, "location_type");
        cJSON *finished_at     = cJSON_GetObjectItemCaseSensitive(alert, "finished_at");

        if (!cJSON_IsString(location_oblast)) {
            continue; // неповний/несподіваний запис - пропускаємо, а не падаємо
        }

        // Нас цікавлять лише тривоги рівня "область" (не район/громада) і
        // лише ті, що ще не завершились (finished_at відсутнє або null).
        bool is_oblast_level = cJSON_IsString(location_type) &&
                                strcmp(location_type->valuestring, "oblast") == 0;
        bool is_active = (finished_at == NULL) || cJSON_IsNull(finished_at);

        if (!is_oblast_level || !is_active) {
            continue;
        }

        int idx = find_region_index(location_oblast->valuestring);
        if (idx < 0) {
            continue; // назва не знайдена в наших даних мапи - пропускаємо, це не помилка
        }

        if (selected_oblast_name != NULL && strcmp(location_oblast->valuestring, selected_oblast_name) == 0) {
            result.alarm_active_selected = true;
        }

        bool already_present = false;
        for (int i = 0; i < result.active_region_count; i++) {
            if (result.active_region_indices[i] == idx) {
                already_present = true;
                break;
            }
        }
        if (!already_present && result.active_region_count < ALERTS_MAX_ACTIVE_REGIONS) {
            result.active_region_indices[result.active_region_count++] = idx;
        }
        // якщо масив уже заповнений - решту просто ігноруємо (захист від переповнення)
    }

    cJSON_Delete(root);
    *out = result;
    return true;
}
