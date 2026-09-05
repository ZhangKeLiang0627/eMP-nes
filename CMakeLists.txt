cmake_minimum_required(VERSION 3.10)
project(eMP_nes)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_C_STANDARD 99)
# SimpleNES core is old C++11 code; build everything at C++11 to avoid
# C++17 keyword churn in the core (GCC 6.4.1 musl on T113), same as eMP-gba.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Compiler / linker flags ----
# -O3: SimpleNES is a CPU-bound 6502 interpreter; O3 over O2 helps on
# Cortex-A7. -g -funwind-tables -rdynamic -fno-omit-frame-pointer: keep the
# ARM frame-pointer chain so a crash pc/lr/fp can be resolved offline with
# addr2line (see src/HAL/HAL.cpp signal handler).
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g -funwind-tables -rdynamic -fno-omit-frame-pointer -Wall -Wno-unused-function -Wno-unused-variable -Wno-return-type -Wno-sign-compare")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_C_FLAGS}")

set(SIMPLE_NES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/simplenes)

# ---- SimpleNES core compatibility patches (idempotent) ----
# The pinned upstream targets modern libstdc++; the T113 toolchain is GCC
# 6.4.1 musl. Patches live in cmake/ and are applied to the submodule
# worktree at configure time (the gitlink itself stays pristine).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply-core-patches.sh AND EXISTS ${SIMPLE_NES_DIR}/src/APU/APU.cpp)
    execute_process(
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply-core-patches.sh ${SIMPLE_NES_DIR}
        RESULT_VARIABLE PATCH_RESULT
    )
    if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply SimpleNES compatibility patches")
    endif()
endif()

# ---- Include paths ----
# Order matters: src/sf_shim MUST come before ${SIMPLE_NES_DIR}/include so
# the SimpleNES core picks up our sn::AudioPlayer stand-in ("AudioPlayer.h"
# is quoted from include/APU/APU.h and does not exist next to it).
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sf_shim
    ${CMAKE_CURRENT_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nes_port
    ${SIMPLE_NES_DIR}/include
)

# ---- LVGL config ----
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
add_definitions(-DLV_USE_LINUX_FBDEV=1 -DLV_USE_EVDEV=1 -DLV_USE_SDL=0)
add_definitions(-DNES_SCREEN_W=480 -DNES_SCREEN_H=480)

# ---- SimpleNES core: SFML-free subset (upstream shell files excluded) ----
# Excluded on purpose (they pull in SFML / miniaudio):
#   main.cpp, Emulator.cpp, VirtualScreen.cpp, AudioPlayer.cpp,
#   KeybindingsParser.cpp, test/, vendor/
set(NES_CORE_SOURCES
    ${SIMPLE_NES_DIR}/src/APU/APU.cpp
    ${SIMPLE_NES_DIR}/src/APU/DMC.cpp
    ${SIMPLE_NES_DIR}/src/APU/FrameCounter.cpp
    ${SIMPLE_NES_DIR}/src/APU/Noise.cpp
    ${SIMPLE_NES_DIR}/src/APU/Pulse.cpp
    ${SIMPLE_NES_DIR}/src/APU/Triangle.cpp
    ${SIMPLE_NES_DIR}/src/APU/Units.cpp
    ${SIMPLE_NES_DIR}/src/CPU.cpp
    ${SIMPLE_NES_DIR}/src/Cartridge.cpp
    ${SIMPLE_NES_DIR}/src/Controller.cpp
    ${SIMPLE_NES_DIR}/src/Log.cpp
    ${SIMPLE_NES_DIR}/src/MainBus.cpp
    ${SIMPLE_NES_DIR}/src/Mapper.cpp
    ${SIMPLE_NES_DIR}/src/MapperAxROM.cpp
    ${SIMPLE_NES_DIR}/src/MapperCNROM.cpp
    ${SIMPLE_NES_DIR}/src/MapperColorDreams.cpp
    ${SIMPLE_NES_DIR}/src/MapperGxROM.cpp
    ${SIMPLE_NES_DIR}/src/MapperMMC3.cpp
    ${SIMPLE_NES_DIR}/src/MapperNROM.cpp
    ${SIMPLE_NES_DIR}/src/MapperSxROM.cpp
    ${SIMPLE_NES_DIR}/src/MapperUxROM.cpp
    ${SIMPLE_NES_DIR}/src/PPU.cpp
    ${SIMPLE_NES_DIR}/src/PictureBus.cpp
)

# ---- Our glue / HAL / UI / main ----
file(GLOB NES_PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/nes_port/*.cpp)
file(GLOB NES_FS_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/src/fs/*.c)
file(GLOB NES_HAL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/HAL/*.cpp)
file(GLOB NES_PAGE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Page/*.cpp)

set(ALL_SRCS
    ${NES_CORE_SOURCES}
    ${NES_PORT_SOURCES}
    ${NES_FS_SOURCES}
    ${NES_HAL_SOURCES}
    ${NES_PAGE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

add_executable(${PROJECT_NAME} ${ALL_SRCS})

# ---- LVGL (v9.4): lv_conf.h lives in libs/ ----
set(LV_BUILD_CONF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs CACHE PATH "Directory containing lv_conf.h")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    lvgl
    pthread
    m
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)
