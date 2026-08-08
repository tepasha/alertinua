file(GLOB_RECURSE SRC_SOURCES "*.cpp" "*.c")
file(GLOB_RECURSE INC_SOURCES "../include/*.cpp" "../include/*.c")

idf_component_register(
    SRCS 
        ${SRC_SOURCES}
        ${INC_SOURCES}
    INCLUDE_DIRS 
        "."
        "../include"
        "../include/map"
)
