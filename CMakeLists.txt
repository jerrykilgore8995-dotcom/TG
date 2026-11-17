cmake_minimum_required(VERSION 3.25)
project(TG VERSION 0.1.0 LANGUAGES C CXX)

set(TG_JUCE_PATH "" CACHE PATH "Optional local JUCE 8.0.6 checkout")
set(TG_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE PATH "Directory containing generated TypeDModel.h")
if(NOT EXISTS "${TG_MODEL_DIR}/TypeDModel.h")
    message(FATAL_ERROR "Generate TypeDModel.h with spice/characterize.py and set TG_MODEL_DIR.")
endif()

if(TG_JUCE_PATH)
    add_subdirectory("${TG_JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(TG
    COMPANY_NAME "TG"
    BUNDLE_ID "audio.tg.typed"
    PLUGIN_MANUFACTURER_CODE Tgpr
    PLUGIN_CODE Tgtd
    PRODUCT_NAME "TG Type D Preview"
    FORMATS VST3
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TG PRIVATE plugin/Processor.cpp plugin/Editor.cpp)
target_include_directories(TG PRIVATE "${TG_MODEL_DIR}")
target_compile_features(TG PRIVATE cxx_std_20)
target_compile_definitions(TG PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TG PRIVATE
    juce::juce_audio_processors juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
