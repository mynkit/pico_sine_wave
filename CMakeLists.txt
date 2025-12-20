cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == Pico VS Code Extension boilerplate ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ======================================

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

set(PICO_EXTRAS_PATH "$ENV{HOME}/pico/pico-extras")
include(pico_extras_import.cmake)

project(sine_wave C CXX ASM)

pico_sdk_init()

# ---- executable ----
add_executable(sine_wave
    sine_wave.c
)

pico_set_program_name(sine_wave "sine_wave")
pico_set_program_version(sine_wave "0.1")

pico_enable_stdio_uart(sine_wave 0)
pico_enable_stdio_usb(sine_wave 0)

# ---- libraries ----
target_link_libraries(sine_wave
    pico_stdlib
)

# ---- I2S only if available ----
if (TARGET pico_audio_i2s)
    target_link_libraries(sine_wave
        pico_audio_i2s
        hardware_pio
        hardware_dma
    )

    target_compile_definitions(sine_wave PRIVATE
        USE_AUDIO_I2S=1
        PICO_AUDIO_I2S_MONO_INPUT=1
        PICO_AUDIO_I2S_DATA_PIN=22
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=23
    )
endif()


pico_add_extra_outputs(sine_wave)
