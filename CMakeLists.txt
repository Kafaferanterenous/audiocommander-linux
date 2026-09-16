cmake_minimum_required(VERSION 3.16)
project(AudioCommanderLinux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)

if(EXISTS "/usr/lib/x86_64-linux-gnu/libfluidsynth.so.3")
    set(FLUIDSYNTH_LIB "/usr/lib/x86_64-linux-gnu/libfluidsynth.so.3")
    set(HAS_FLUIDSYNTH TRUE)
    message(STATUS "FluidSynth found: ${FLUIDSYNTH_LIB}")
else()
    find_library(FLUIDSYNTH_LIB NAMES fluidsynth)
    if(FLUIDSYNTH_LIB)
        set(HAS_FLUIDSYNTH TRUE)
        message(STATUS "FluidSynth found: ${FLUIDSYNTH_LIB}")
    else()
        set(HAS_FLUIDSYNTH FALSE)
        message(STATUS "FluidSynth not found - MIDI playback disabled")
    endif()
endif()

set(AC_CORE_SOURCES
    src/browser.c
    src/playlist.c
    src/localization.c
    src/settings.c
    src/skin.c
    src/media_info.c
    src/ini.c
    src/utf8.c
    src/file_ops.c
    src/copy_progress.c
    src/playback.c)

add_library(ac_core STATIC ${AC_CORE_SOURCES})
target_include_directories(ac_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ac_core PUBLIC PkgConfig::AVFORMAT PkgConfig::AVCODEC
    PkgConfig::AVUTIL PkgConfig::SWRESAMPLE PkgConfig::PULSE
    PkgConfig::PULSE_SIMPLE pthread)
if(HAS_FLUIDSYNTH)
    target_compile_definitions(ac_core PUBLIC HAS_FLUIDSYNTH)
    target_link_libraries(ac_core PUBLIC ${FLUIDSYNTH_LIB})
endif()

add_executable(audiocommander src/app.c)
target_link_libraries(audiocommander PRIVATE ac_core ${GTK3_LIBRARIES})
target_include_directories(audiocommander PRIVATE ${GTK3_INCLUDE_DIRS})
if(GTK3_LIBRARY_DIRS)
    target_link_directories(audiocommander PRIVATE ${GTK3_LIBRARY_DIRS})
endif()

enable_testing()

add_executable(test_browser tests/test_browser.c)
target_link_libraries(test_browser PRIVATE ac_core)
add_test(NAME test_browser COMMAND test_browser)

add_executable(test_playlist tests/test_playlist.c)
target_link_libraries(test_playlist PRIVATE ac_core)
add_test(NAME test_playlist COMMAND test_playlist)

add_executable(test_localization tests/test_localization.c)
target_link_libraries(test_localization PRIVATE ac_core)
add_test(NAME test_localization COMMAND test_localization)

add_executable(test_settings tests/test_settings.c)
target_link_libraries(test_settings PRIVATE ac_core)
target_compile_definitions(test_settings PRIVATE
    SKINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/skins")
add_test(NAME test_settings COMMAND test_settings)

add_executable(test_file_ops tests/test_file_ops.c)
target_link_libraries(test_file_ops PRIVATE ac_core)
add_test(NAME test_file_ops COMMAND test_file_ops)

add_executable(test_media_info tests/test_media_info.c)
target_link_libraries(test_media_info PRIVATE ac_core)
add_test(NAME test_media_info COMMAND test_media_info)

find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    set(SMOKE_LEFT ${CMAKE_CURRENT_BINARY_DIR}/smoke_left)
    set(SMOKE_RIGHT ${CMAKE_CURRENT_BINARY_DIR}/smoke_right)
    file(MAKE_DIRECTORY ${SMOKE_LEFT} ${SMOKE_RIGHT})
    file(WRITE ${SMOKE_LEFT}/move-me.mp3 "audio-payload")
    add_test(NAME gui_smoke
             COMMAND ${CMAKE_COMMAND} -E env
                     AUDIOCOMMANDER_GUI_SMOKE=1
                     AUDIOCOMMANDER_SMOKE_LEFT=${SMOKE_LEFT}
                     AUDIOCOMMANDER_SMOKE_RIGHT=${SMOKE_RIGHT}
                     xvfb-run -s "-screen 0 1920x1080x24" -a $<TARGET_FILE:audiocommander>)
endif()
