cmake_minimum_required(VERSION 3.16.0)

# Задачі FreeRTOS лежать у корені проєкту (task/<назва>_task/, кожна -
# окремий компонент), а не в components/, тому цю папку треба явно
# додати до пошуку компонентів. Саме list(APPEND), а не set(): PlatformIO
# вже кладе в EXTRA_COMPONENT_DIRS папку src, і set() її б затер.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/task")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(alertinua)
