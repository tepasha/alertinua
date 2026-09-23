# Хост-тести (запускаються тут, на робочій машині, БЕЗ ESP-IDF і без плати).
# Компілюють РЕАЛЬНІ файли прошивки напряму:
#   - components/scraping/alerts_parser.c      (потрібен libcjson-dev)
#   - components/brightness_ctrl/brightness_ctrl_step.c (без залежностей)
# Юніт-тест фреймворк - справжній ThrowTheSwitch/Unity (той самий, що й
# всередині ESP-IDF), ввендорено в test_host/unity/.
#
# Використання:
#   make -C test_host            # збірка обох наборів тестів
#   make -C test_host run        # збірка + запуск обох (з кодом виходу)
#   make -C test_host clean
#
# Потрібно: gcc, libcjson-dev (apt-get install --no-install-recommends libcjson-dev)

CC      ?= gcc
ROOT    := ..
BUILD   := build
CFLAGS  := -std=c11 -Wall -Wextra -g -O0
UNITY_SRC := unity/unity.c

.PHONY: all run clean

all: $(BUILD)/test_alerts_parser $(BUILD)/test_brightness_ctrl

$(BUILD):
	mkdir -p $(BUILD)

# --- alerts_parser: потребує cJSON (системний libcjson-dev) + esp_log.h-заглушку ---
$(BUILD)/test_alerts_parser: test_alerts_parser.c $(UNITY_SRC) \
		$(ROOT)/components/scraping/alerts_parser.c | $(BUILD)
	$(CC) $(CFLAGS) \
		-I unity -I stubs \
		-I /usr/include/cjson \
		-I $(ROOT)/components/scraping \
		-I $(ROOT)/components/map \
		$^ -lcjson -o $@

# --- brightness_ctrl_step: чиста математика, без жодних залежностей ---
$(BUILD)/test_brightness_ctrl: test_brightness_ctrl.c $(UNITY_SRC) \
		$(ROOT)/components/brightness_ctrl/brightness_ctrl_step.c | $(BUILD)
	$(CC) $(CFLAGS) \
		-I unity \
		-I $(ROOT)/components/brightness_ctrl \
		$^ -lm -o $@

run: all
	@echo "=== test_alerts_parser ===" && ./$(BUILD)/test_alerts_parser
	@echo "=== test_brightness_ctrl ===" && ./$(BUILD)/test_brightness_ctrl

clean:
	rm -rf $(BUILD)
