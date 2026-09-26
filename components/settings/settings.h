#pragma once
#include <stddef.h>
#include "esp_err.h"

/*
 * Налаштування пристрою, які користувач змінює через веб-інтерфейс і які
 * мають пережити перезавантаження (зберігаються в NVS).
 */

#define SETTINGS_OBLAST_MAX_LEN 64 // з запасом: найдовша назва ~50 байт у UTF-8

/*
 * Назва області для стеження (як у списку локацій API, напр. "Київська
 * область" чи "м. Київ"). Якщо в NVS нічого не збережено - значення
 * CONFIG_ALERTINUA_OBLAST_NAME з menuconfig.
 */
void settings_get_oblast(char *out, size_t out_size);

/* Зберігає назву області в NVS. Перевірку, що назва існує, робить викликач. */
esp_err_t settings_set_oblast(const char *name);
