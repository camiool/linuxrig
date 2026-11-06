cmake_minimum_required(VERSION 3.22)
project(LinuxRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE se descarga en el primer configure. Versión fijada para builds reproducibles.
set(LINUXRIG_JUCE_TAG "8.0.15" CACHE STRING "Tag de JUCE a usar")
option(LINUXRIG_SANITIZE "Compilar con AddressSanitizer + UBSan" OFF)

if(LINUXRIG_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${LINUXRIG_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(LINUXRIG_COMMON_DEFINITIONS
    JUCE_PLUGINHOST_LV2=0
    JUCE_JACK=1
    JUCE_ALSA=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

set(LINUXRIG_RECOMMENDED_FLAGS
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Motor y presets: sin UI ni hosting, se comparten con los tests.
set(LINUXRIG_ENGINE_SOURCES
    src/engine/Slot.cpp
    src/engine/ChainProcessor.cpp
    src/core/Preset.cpp)

# --- App -----------------------------------------------------------------------
juce_add_gui_app(LinuxRig
    PRODUCT_NAME "LinuxRig"
    COMPANY_NAME "LinuxRig")

target_sources(LinuxRig PRIVATE
    ${LINUXRIG_ENGINE_SOURCES}
    src/engine/AudioEngine.cpp
    src/core/PluginCatalog.cpp
    src/core/Rig.cpp
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/Pedalboard.cpp)

target_compile_definitions(LinuxRig PRIVATE
    ${LINUXRIG_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LinuxRig,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LinuxRig,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_JACK_CLIENT_NAME="LinuxRig")

target_link_libraries(LinuxRig
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
    PUBLIC
        ${LINUXRIG_RECOMMENDED_FLAGS})

# --- Tests ---------------------------------------------------------------------
juce_add_console_app(LinuxRigTests
    PRODUCT_NAME "LinuxRig Tests")

target_sources(LinuxRigTests PRIVATE
    ${LINUXRIG_ENGINE_SOURCES}
    tests/Tests.cpp)

target_compile_definitions(LinuxRigTests PRIVATE
    ${LINUXRIG_COMMON_DEFINITIONS}
    JUCE_PLUGINHOST_VST3=0)

target_link_libraries(LinuxRigTests
    PRIVATE
        juce::juce_audio_processors_headless
    PUBLIC
        ${LINUXRIG_RECOMMENDED_FLAGS})

enable_testing()
add_test(NAME LinuxRigTests COMMAND LinuxRigTests)

# --- Fase 0: prueba de concepto (entrada → 1 VST3 → salida) --------------------
juce_add_gui_app(LinuxRigPoC
    PRODUCT_NAME "LinuxRig PoC"
    COMPANY_NAME "LinuxRig")

target_sources(LinuxRigPoC PRIVATE
    src/poc/Main.cpp
    src/poc/AudioEngine.cpp
    src/poc/MainComponent.cpp)

target_compile_definitions(LinuxRigPoC PRIVATE
    ${LINUXRIG_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LinuxRigPoC,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LinuxRigPoC,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_JACK_CLIENT_NAME="LinuxRigPoC")

target_link_libraries(LinuxRigPoC
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
    PUBLIC
        ${LINUXRIG_RECOMMENDED_FLAGS})
