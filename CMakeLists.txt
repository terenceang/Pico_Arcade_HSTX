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

project(Pico_Arcade_HSTX VERSION 1.4.0 LANGUAGES C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Move pico_hdmi's active-line DMA buffer (line_buffer in video_output.c)
# out of regular SRAM into a dedicated scratch-Y bank Core 0's working set
# doesn't normally touch - an attempt at actually preventing the HDMI
# sync-loss bug's Core 0/Core 1 bus-contention trigger, rather than just
# detecting and recovering from it after the fact. See CLAUDE.md's "HSTX
# sync-loss caveat".
set(PICO_HDMI_LINE_BUFFER_IN_SCRATCH_Y ON CACHE BOOL "Place the HDMI active-line DMA buffer in scratch Y RAM" FORCE)
set(PICO_HDMI_PRECOMPOSED_ACTIVE_LINES ON CACHE BOOL "Enable precomposed HDMI active-line templates" FORCE)

# Add pico_hdmi library for HSTX HDMI video & audio
add_subdirectory(lib/pico_hdmi)

# ----------------------------------------------------------------------------
# Space Invaders arcade ROM: embedded into the flash image at build time from
# files the user supplies locally in roms/space_invaders/ (not vendored - it's Taito's
# copyrighted work, see roms/space_invaders/README.md). If missing, a zero-filled
# placeholder is generated instead so the rest of the project still builds -
# see cmake/generate_space_invaders_rom.cmake.
set(SI_ROM_DIR ${CMAKE_CURRENT_LIST_DIR}/roms/space_invaders)
set(SI_ROM_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/space_invaders_rom_data.c)
add_custom_target(si_generate_rom ALL
        COMMAND ${CMAKE_COMMAND}
                -DSI_ROM_DIR=${SI_ROM_DIR}
                -DSI_ROM_OUT=${SI_ROM_GEN}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_space_invaders_rom.cmake
        BYPRODUCTS ${SI_ROM_GEN}
        COMMENT "Embedding Space Invaders arcade ROM (roms/space_invaders/) into flash image"
)

# ----------------------------------------------------------------------------
# Sound-effect PCM data: embedded at build time from roms/space_invaders/
set(SI_SOUND_DIR ${CMAKE_CURRENT_LIST_DIR}/roms/space_invaders)
set(SI_SOUND_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/space_invaders_sound_data.c)
add_custom_target(si_generate_sounds ALL
        COMMAND ${CMAKE_COMMAND}
                -DSI_SOUND_DIR=${SI_SOUND_DIR}
                -DSI_SOUND_OUT=${SI_SOUND_GEN}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_space_invaders_sounds.cmake
        BYPRODUCTS ${SI_SOUND_GEN}
        COMMENT "Embedding Space Invaders sound PCM data (roms/space_invaders/) into flash image"
)

# ----------------------------------------------------------------------------
# Pac-Man arcade ROM and PROMs: embedded at build time from roms/pacman/
set(PACMAN_ROM_DIR ${CMAKE_CURRENT_LIST_DIR}/roms/pacman)
set(PACMAN_ROM_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/pacman_rom_data.c)
add_custom_target(pacman_generate_rom ALL
        COMMAND ${CMAKE_COMMAND}
                -DPACMAN_ROM_DIR=${PACMAN_ROM_DIR}
                -DPACMAN_ROM_OUT=${PACMAN_ROM_GEN}
                -P ${CMAKE_CURRENT_LIST_DIR}/cmake/generate_pacman_rom.cmake
        BYPRODUCTS ${PACMAN_ROM_GEN}
        COMMENT "Embedding Pac-Man arcade ROMs and PROMs (roms/pacman/) into flash image"
)

# Add executable.
add_executable(Pico_Arcade_HSTX
        src/main.c
        src/core/plugin_registry.c
        src/platform/input/host_input.c
        src/platform/input/snes_controller.c
        src/platform/audio/audio_engine.c
        src/video/dvi_display.c
        src/video/testcard.c
        src/video/controller_testcard.c
        src/video/missing_rom_screen.c
        src/video/screen_capture.c
        src/audio/sound_effects.c
        src/audio/namco_sound.c
        src/emu/z80emu.c
        src/emu/space_invaders_machine.c
        src/emu/pacman_machine.c
        src/games/space_invaders/space_invaders_video.c
        src/games/space_invaders/space_invaders_plugin.c
        src/games/pacman/pacman_video.c
        src/games/pacman/pacman_plugin.c
        ${SI_ROM_GEN}
        ${SI_SOUND_GEN}
        ${PACMAN_ROM_GEN}
)
add_dependencies(Pico_Arcade_HSTX si_generate_rom si_generate_sounds pacman_generate_rom)

# SNES controller PIO driver (src/platform/input/snes_controller.pio)
pico_generate_pio_header(Pico_Arcade_HSTX ${CMAKE_CURRENT_LIST_DIR}/src/platform/input/snes_controller.pio)

pico_set_program_name(Pico_Arcade_HSTX "Pico_Arcade_HSTX")
pico_set_program_version(Pico_Arcade_HSTX "${PROJECT_VERSION}")

# USB stdio enabled for picotool automatic reboot/flash support (zero printf in runtime)
pico_enable_stdio_uart(Pico_Arcade_HSTX 0)
pico_enable_stdio_usb(Pico_Arcade_HSTX 1)
pico_set_binary_type(Pico_Arcade_HSTX copy_to_ram)

target_link_libraries(Pico_Arcade_HSTX
        pico_stdlib
        pico_multicore
        pico_util
        hardware_clocks
        hardware_vreg
        hardware_dma
        hardware_pio
        pico_hdmi
)

target_include_directories(Pico_Arcade_HSTX PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/core
        ${CMAKE_CURRENT_LIST_DIR}/src/video
        ${CMAKE_CURRENT_LIST_DIR}/src/audio
        ${CMAKE_CURRENT_LIST_DIR}/src/emu
        ${CMAKE_CURRENT_LIST_DIR}/src/platform
        ${CMAKE_CURRENT_LIST_DIR}/src/games/space_invaders
        ${CMAKE_CURRENT_LIST_DIR}/src/games/pacman
)

target_compile_definitions(Pico_Arcade_HSTX PRIVATE
        PICO_ARCADE_HSTX_VERSION="${PROJECT_VERSION}"
)

target_compile_options(Pico_Arcade_HSTX PRIVATE
        -O3
)

pico_add_extra_outputs(Pico_Arcade_HSTX)


