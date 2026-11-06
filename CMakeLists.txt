cmake_minimum_required(VERSION 3.22)
project(LinuxRig VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE se descarga en el primer configure. Versión fijada para builds reproducibles.
set(LINUXRIG_JUCE_TAG "8.0.15" CACHE STRING "Tag de JUCE a usar")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${LINUXRIG_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Fase 0: prueba de concepto (entrada → 1 VST3 → salida) -------------------
juce_add_gui_app(LinuxRigPoC
    PRODUCT_NAME "LinuxRig PoC"
    COMPANY_NAME "LinuxRig")

target_sources(LinuxRigPoC PRIVATE
    src/poc/Main.cpp
    src/poc/AudioEngine.cpp
    src/poc/MainComponent.cpp)

target_compile_definitions(LinuxRigPoC PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LinuxRigPoC,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LinuxRigPoC,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_JACK=1
    JUCE_JACK_CLIENT_NAME="LinuxRig"
    JUCE_ALSA=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(LinuxRigPoC
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
