cmake_minimum_required(VERSION 3.16)
project(ScooterV2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RASPI_HW "Build with Raspberry Pi GPIO/ADC hardware support" OFF)

# --- Sources ---
set(SOURCES
    src/main.cpp
    src/sound_loader.cpp
    src/m4_sound_manager.cpp
    src/supra_sound_manager.cpp
    src/hellcat_sound_manager.cpp
    src/svj_sound_manager.cpp
    src/m4_engine.cpp
    src/supra_engine.cpp
    src/hellcat_engine.cpp
    src/svj_engine.cpp
    src/car_system.cpp
    src/hardware.cpp
    src/logging.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# --- SDL2 + SDL2_mixer ---
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer
)

# --- Raspberry Pi hardware (optional) ---
if(RASPI_HW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RASPI_HW)
    find_library(WIRINGPI_LIB wiringPi)
    if(WIRINGPI_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WIRINGPI_LIB})
    else()
        message(WARNING "wiringPi not found -- GPIO/ADC calls will fail at link time")
    endif()
endif()

# --- Platform-specific threading ---
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# --- Real-time scheduling on Linux (nice-to-have) ---
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE rt)
endif()

# --- Compiler warnings ---
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
