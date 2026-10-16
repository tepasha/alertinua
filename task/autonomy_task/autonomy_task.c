#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "driver/rtc_io.h"

#include "app_state.h"
#include "backlight.h"
#include "button.h"
#include "buzzer_task.h"
#include "fetch_task.h"
#include "render_task.h"
#include "settings.h"
#include "wifi_creds.h"
#include "wifi_manager.h"
#include "autonomy_task.h"

#ifdef CONFIG_ALERTINUA_AUTONOMY_ENABLE

static const char *TAG = "AUTONOMY";

#ifndef CONFIG_ALERTINUA_DEFAULT_WIFI_SSID
#define CONFIG_ALERTINUA_DEFAULT_WIFI_SSID "alertinua"
#endif
#ifndef CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD
#define CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD "alertinua"
#endif

/* Чи була тривога у вибраній області на момент засинання. RTC_DATA_ATTR -
 * змінна живе в RTC-пам'яті і переживає deep sleep: так пробудження
 * таймером будить пристрій лише на ПОЧАТКУ тривоги, а не на кожній
 * перевірці, поки вона триває. Після ввімкнення живлення - false. */
static RTC_DATA_ATTR bool s_alarm_before_sleep = false;

/* Пише button_task, читає autonomy_task. TickType_t - 32 біти, тож запис і
 * читання атомарні на ESP32 без mutex. */
static volatile TickType_t s_last_activity_tick = 0;

static void enter_deep_sleep(void) __attribute__((noreturn));
static void enter_deep_sleep(void) {
    ESP_LOGI(TAG, "засинаю на %d с (тривога до сну: %d)",
             CONFIG_ALERTINUA_AUTONOMY_POLL_SEC, (int)s_alarm_before_sleep);

    render_task_display_sleep();
    backlight_off_for_deep_sleep();
    esp_wifi_stop(); // ESP_ERR_WIFI_NOT_INIT, якщо Wi-Fi не запускали, - не помилка

    esp_sleep_enable_timer_wakeup((uint64_t)CONFIG_ALERTINUA_AUTONOMY_POLL_SEC * 1000000ULL);
    // Кнопка BOOT активна низьким рівнем. З ext0 RTC-периферія у сні
    // лишається живлена, тож внутрішня підтяжка вгору працює.
    esp_sleep_enable_ext0_wakeup(SETUP_BUTTON_GPIO, 0);
    rtc_gpio_pullup_en(SETUP_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(SETUP_BUTTON_GPIO);

    esp_deep_sleep_start();
}

static bool wifi_connect_quick(void) {
    char ssid[WIFI_CREDS_SSID_MAX_LEN] = CONFIG_ALERTINUA_DEFAULT_WIFI_SSID;
    char password[WIFI_CREDS_PASS_MAX_LEN] = CONFIG_ALERTINUA_DEFAULT_WIFI_PASSWORD;
    wifi_creds_load(ssid, password); // немає в NVS - лишаються значення з Kconfig

    if (!wifi_manager_begin_connect(ssid, password)) {
        return false;
    }
    for (int waited_s = 0; waited_s < CONFIG_ALERTINUA_AUTONOMY_WIFI_TIMEOUT_SEC; waited_s++) {
        wifi_connect_result_t r = wifi_manager_wait_connect(1000);
        if (r != WIFI_CONNECT_PENDING) {
            return r == WIFI_CONNECT_OK;
        }
    }
    return false;
}

void autonomy_handle_wakeup(QueueHandle_t alert_queue) {
    // Після пробудження по ext0 пін кнопки лишається RTC IO - повернути його
    // цифровому GPIO, інакше button_task не бачитиме натискань.
    rtc_gpio_deinit(SETUP_BUTTON_GPIO);

    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) {
        ESP_LOGI(TAG, "розбудила кнопка - звичайна робота");
        return;
    }
    if (!(causes & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
        return; // ввімкнення живлення чи перезавантаження - звичайна робота
    }

    // Перевірка уві сні: дисплей і підсвітка не вмикаються (панель спить,
    // пін підсвітки утримується в 0 з попереднього засинання).
    ESP_LOGI(TAG, "пробудження таймером - перевіряю тривоги");

    if (!wifi_connect_quick()) {
        ESP_LOGW(TAG, "Wi-Fi недоступний - сплю до наступної перевірки");
        enter_deep_sleep();
    }

    char oblast[SETTINGS_OBLAST_MAX_LEN];
    settings_get_oblast(oblast, sizeof(oblast));

    alert_msg_t msg;
    if (!fetch_alerts_once(oblast, &msg)) {
        // Стан тривоги невідомий - s_alarm_before_sleep не чіпаємо.
        enter_deep_sleep();
    }

    bool alarm = msg.parsed.alarm_active_selected;
    bool alarm_started = alarm && !s_alarm_before_sleep;
    s_alarm_before_sleep = alarm;

    if (!alarm_started) {
        ESP_LOGI(TAG, "%s у \"%s\" - не прокидаюсь", alarm ? "тривога триває" : "тривоги немає", oblast);
        enter_deep_sleep();
    }

    ESP_LOGW(TAG, "почалась тривога у \"%s\" - прокидаюсь", oblast);
    app_state_set_alarm(true, msg.parsed.active_region_indices, msg.parsed.active_region_count);
    app_state_set(APP_STATE_ALARM);
    // render_task ще не запущена, але черга вже є: щойно задача стартує,
    // вона намалює мапу й увімкне сирену, не чекаючи свого опитування.
    xQueueSend(alert_queue, &msg, 0);
}

void autonomy_notify_activity(void) {
    s_last_activity_tick = xTaskGetTickCount();
}

static void autonomy_task(void *arg) {
    (void)arg;
    esp_task_wdt_add(NULL);

    const TickType_t idle_ticks = pdMS_TO_TICKS(CONFIG_ALERTINUA_AUTONOMY_IDLE_SEC * 1000);

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Не засинати посеред налаштування Wi-Fi чи сирени.
        if (wifi_manager_is_provisioning() || buzzer_is_alarm_playing()) {
            continue;
        }
        if (xTaskGetTickCount() - s_last_activity_tick < idle_ticks) {
            continue;
        }

        ESP_LOGI(TAG, "%d с без натискань кнопки", CONFIG_ALERTINUA_AUTONOMY_IDLE_SEC);
        s_alarm_before_sleep = app_state_is_alarm_active(); // цю тривогу користувач уже бачив
        enter_deep_sleep();
    }
}

void autonomy_task_start(void) {
    autonomy_notify_activity(); // відлік - від старту, а не від нуля тіків
    xTaskCreate(autonomy_task, "autonomy_task", 3072, NULL, 2, NULL);
}

#else /* !CONFIG_ALERTINUA_AUTONOMY_ENABLE */

void autonomy_handle_wakeup(QueueHandle_t alert_queue) {
    (void)alert_queue;
}

void autonomy_task_start(void) {
}

void autonomy_notify_activity(void) {
}

#endif
