cmake_minimum_required(VERSION 3.22)

project(VocalSuite VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.7
)

FetchContent_MakeAvailable(JUCE)

option(VS_COPY_AFTER_BUILD "Copy the plugin into your plug-in folder after building" ON)
set(VS_VST3_INSTALL_DIR "" CACHE PATH "Optional custom destination for the built VST3 plugin")

if(VS_VST3_INSTALL_DIR)
  set_property(GLOBAL PROPERTY JUCE_VST3_COPY_DIR "${VS_VST3_INSTALL_DIR}")
endif()

if(APPLE)
  set(VS_FORMATS VST3 AU Standalone)
else()
  set(VS_FORMATS VST3 Standalone)
endif()

juce_add_plugin(VocalSuite
  COMPANY_NAME "YourName"
  PLUGIN_MANUFACTURER_CODE Yrnm
  PLUGIN_CODE Vsui
  FORMATS ${VS_FORMATS}
  PRODUCT_NAME "VocalSuite"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${VS_COPY_AFTER_BUILD}
)

juce_generate_juce_header(VocalSuite)

target_sources(VocalSuite PRIVATE
  Source/VocalSuite.cpp
)

target_compile_definitions(VocalSuite PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(VocalSuite
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags
         juce::juce_recommended_lto_flags
         juce::juce_recommended_warning_flags
)

target_compile_features(VocalSuite PRIVATE cxx_std_17)

if(APPLE)
  list(APPEND VS_PACKAGE_PLUGINS "AU/VocalSuite.component")
  set(VS_PACKAGE_TARGETS VocalSuite_VST3 VocalSuite_AU)
else()
  set(VS_PACKAGE_TARGETS VocalSuite_VST3)
endif()

list(PREPEND VS_PACKAGE_PLUGINS "VST3/VocalSuite.vst3")

set(VS_PACKAGE_NAME "VocalSuite-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}.zip")
add_custom_target(package
  COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/${VS_PACKAGE_NAME}"
  COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${VS_PACKAGE_NAME}"
          --format=zip ${VS_PACKAGE_PLUGINS}
  DEPENDS ${VS_PACKAGE_TARGETS}
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/VocalSuite_artefacts/$<CONFIG>"
  COMMENT "Packaging VocalSuite plugin as ${VS_PACKAGE_NAME}"
  VERBATIM
)
