# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This is firmware for a resource-constrained embedded target: default to an
# optimized build when the documented build command (README/CLAUDE.md) is
# used as-is, without requiring -DCMAKE_BUILD_TYPE on the command line.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Space_Invader_PICO VERSION 1.0.0 LANGUAGES C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add pico_hdmi library for HSTX HDMI video & audio
add_subdirectory(lib/pico_hdmi)

# ----------------------------------------------------------------------------
# Space Invaders arcade ROM: embedded into the flash image at build time from
# files the user supplies locally in roms/ (not vendored - it's Taito's
# copyrighted work, see roms/README.md). If missing, a zero-filled
# placeholder is generated instead so the rest of the project still builds -
# see cmake/generate_rom.cmake.
set(SI_ROM_DIR ${CMAKE_CURRENT_LIST_DIR}/roms)
set(SI_ROM_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/rom_data.c)
add_custom_target(si_generate_rom ALL
        COMMAND ${CMAKE_COMMAND}
                -DSI_ROM_DIR=${SI_ROM_DIR}
                -DSI_ROM_OUT=${SI_ROM_GEN}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_rom.cmake
        BYPRODUCTS ${SI_ROM_GEN}
        COMMENT "Embedding Space Invaders arcade ROM (roms/) into flash image"
)

# ----------------------------------------------------------------------------
# Sound-effect PCM data
set(SI_SOUND_DIR ${CMAKE_CURRENT_LIST_DIR}/sounds)
set(SI_SOUND_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/sound_data.c)
add_custom_target(si_generate_sounds ALL
        COMMAND ${CMAKE_COMMAND}
                -DSI_SOUND_DIR=${SI_SOUND_DIR}
                -DSI_SOUND_OUT=${SI_SOUND_GEN}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_sounds.cmake
        BYPRODUCTS ${SI_SOUND_GEN}
        COMMENT "Embedding sound-effect PCM data (sounds/) into flash image"
)

# Add executable.
add_executable(Space_Invader_PICO
        src/main.c
        src/video/dvi_display.c
        src/video/testcard.c
        src/video/controller_testcard.c
        src/game.c
        src/audio/audio_i2s.c
        src/audio/sound_effects.c
        src/emu/i8080.c
        src/emu/invaders_machine.c
        src/input/snes_controller.c
        ${SI_ROM_GEN}
        ${SI_SOUND_GEN}
)
add_dependencies(Space_Invader_PICO si_generate_rom si_generate_sounds)

pico_set_program_name(Space_Invader_PICO "Space_Invader_PICO")
pico_set_program_version(Space_Invader_PICO "${PROJECT_VERSION}")

pico_enable_stdio_uart(Space_Invader_PICO 1)
pico_enable_stdio_usb(Space_Invader_PICO 1)

target_link_libraries(Space_Invader_PICO
        pico_stdlib
        pico_multicore
        pico_util
        hardware_clocks
        hardware_vreg
        hardware_dma
        hardware_pio
        hardware_pwm
        pico_hdmi
)

target_include_directories(Space_Invader_PICO PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/video
        ${CMAKE_CURRENT_LIST_DIR}/src/audio
        ${CMAKE_CURRENT_LIST_DIR}/src/emu
        ${CMAKE_CURRENT_LIST_DIR}/src/input
)

target_compile_definitions(Space_Invader_PICO PRIVATE
        SPACE_INVADER_PICO_VERSION="${PROJECT_VERSION}"
        VIDEO_MODE_320x240
)

pico_generate_pio_header(Space_Invader_PICO ${CMAKE_CURRENT_LIST_DIR}/src/input/snes_controller.pio)

pico_add_extra_outputs(Space_Invader_PICO)


