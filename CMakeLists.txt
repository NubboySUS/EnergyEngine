cmake_minimum_required(VERSION 3.26)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)

project(EnergyEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

file(GLOB_RECURSE ENERGYENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.cxx"
)

file(GLOB_RECURSE ENERGYENGINE_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.hh"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.hxx"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.inl"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.ipp"
  "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/*.inc"
)

if(APPLE)
  set(ENERGYENGINE_MAC_WINDOW "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/PlatformWindow_macos.mm")
  if(NOT EXISTS "${ENERGYENGINE_MAC_WINDOW}")
    message(FATAL_ERROR "Expected macOS window implementation not found: ${ENERGYENGINE_MAC_WINDOW}")
  endif()
  list(FILTER ENERGYENGINE_SOURCES EXCLUDE REGEX "/Engine/Main/PlatformWindow\\.cpp$")
  list(APPEND ENERGYENGINE_SOURCES "${ENERGYENGINE_MAC_WINDOW}")
endif()

set(ENERGYENGINE_LINUX_WINDOW "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/PlatformWindow_linux.cpp")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${ENERGYENGINE_LINUX_WINDOW}")
  list(FILTER ENERGYENGINE_SOURCES EXCLUDE REGEX "/Engine/Main/PlatformWindow\\.cpp$")
  list(APPEND ENERGYENGINE_SOURCES "${ENERGYENGINE_LINUX_WINDOW}")
endif()

if(NOT ENERGYENGINE_SOURCES)
  message(FATAL_ERROR "No C/C++ source files found under Engine/. Check that the Engine source tree is beside this CMakeLists.txt.")
endif()

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")

if(WIN32)
  add_executable(EnergyEngine WIN32 ${ENERGYENGINE_SOURCES} ${ENERGYENGINE_HEADERS} EnergyEngine.rc EngineResources.rc)
else()
  add_executable(EnergyEngine ${ENERGYENGINE_SOURCES} ${ENERGYENGINE_HEADERS})
endif()

if(NOT WIN32)
  set(ENERGYENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(ENERGYENGINE_EMBEDDED_SHADER_HEADER "${ENERGYENGINE_GENERATED_DIR}/EmbeddedShaders.generated.h")
  set(ENERGYENGINE_EMBEDDED_SHADER_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/default.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/default.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/missing.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/post.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/post.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/sky.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders/sky.vert.spv"
  )
  add_custom_command(
    OUTPUT "${ENERGYENGINE_EMBEDDED_SHADER_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
      "-DENERGYENGINE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DOUTPUT_HEADER=${ENERGYENGINE_EMBEDDED_SHADER_HEADER}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShaders.cmake"
    DEPENDS ${ENERGYENGINE_EMBEDDED_SHADER_INPUTS}
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedShaders.cmake"
    VERBATIM
  )
  add_custom_target(EnergyEngineEmbeddedShaders DEPENDS "${ENERGYENGINE_EMBEDDED_SHADER_HEADER}")
  add_dependencies(EnergyEngine EnergyEngineEmbeddedShaders)
  target_sources(EnergyEngine PRIVATE "${ENERGYENGINE_EMBEDDED_SHADER_HEADER}")
  target_include_directories(EnergyEngine PRIVATE "${ENERGYENGINE_GENERATED_DIR}")
endif()

# Retain compatibility with code that includes paths relative to the project root.
target_include_directories(EnergyEngine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/vulkan/include")
  target_include_directories(EnergyEngine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/vulkan/include")
endif()

target_link_libraries(EnergyEngine PRIVATE Vulkan::Vulkan Threads::Threads ${CMAKE_DL_LIBS})

if(MSVC)
  # Release: optimized machine code. Debug: direct source-level stepping and quick checks.
  target_compile_options(EnergyEngine PRIVATE
    "$<$<CONFIG:Release>:/O2>"
    "$<$<CONFIG:Release>:/Zi>"
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<CONFIG:Debug>:/Zi>"
    "$<$<CONFIG:Debug>:/RTC1>"
    /MP
  )
  target_link_options(EnergyEngine PRIVATE
    "$<$<CONFIG:Release>:/DEBUG>"
    "$<$<CONFIG:Release>:/OPT:REF>"
    "$<$<CONFIG:Release>:/OPT:ICF>"
  )
else()
  target_compile_options(EnergyEngine PRIVATE
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:Debug>:-Og>"
    "$<$<CONFIG:Debug>:-g3>"
  )
endif()

# Link-time optimization is enabled only when the active compiler/linker supports it.
include(CheckIPOSupported)
check_ipo_supported(RESULT ENERGYENGINE_IPO_SUPPORTED OUTPUT ENERGYENGINE_IPO_ERROR LANGUAGES CXX)
if(ENERGYENGINE_IPO_SUPPORTED)
  set_property(TARGET EnergyEngine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
  message(STATUS "LTO/IPO unavailable for this toolchain: ${ENERGYENGINE_IPO_ERROR}")
endif()

if(APPLE)
  set_target_properties(EnergyEngine PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "EnergyEngine"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nubboy.energyengine"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
  )
  foreach(_framework IN ITEMS Cocoa QuartzCore Metal IOKit)
    find_library(ENERGYENGINE_${_framework}_FRAMEWORK ${_framework} REQUIRED)
    target_link_libraries(EnergyEngine PRIVATE "${ENERGYENGINE_${_framework}_FRAMEWORK}")
  endforeach()
endif()

# Keep game data next to the executable (Linux/Windows) or inside the macOS bundle's Resources directory.
if(APPLE)
  foreach(_asset_dir IN ITEMS Config Shaders Materials)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/${_asset_dir}")
      add_custom_command(TARGET EnergyEngine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/${_asset_dir}"
          "$<TARGET_BUNDLE_CONTENT_DIR:EnergyEngine>/Resources/Engine/${_asset_dir}"
        VERBATIM
      )
    endif()
  endforeach()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/YuY.png")
    add_custom_command(TARGET EnergyEngine POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:EnergyEngine>/Resources/Engine/Main"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/YuY.png"
        "$<TARGET_BUNDLE_CONTENT_DIR:EnergyEngine>/Resources/Engine/Main/YuY.png"
      VERBATIM
    )
  endif()
else()
  foreach(_asset_dir IN ITEMS Config Shaders Materials)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Engine/${_asset_dir}")
      add_custom_command(TARGET EnergyEngine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/${_asset_dir}"
          "$<TARGET_FILE_DIR:EnergyEngine>/Engine/${_asset_dir}"
        VERBATIM
      )
    endif()
  endforeach()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/YuY.png")
    add_custom_command(TARGET EnergyEngine POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:EnergyEngine>/Engine/Main"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Main/YuY.png"
        "$<TARGET_FILE_DIR:EnergyEngine>/Engine/Main/YuY.png"
      VERBATIM
    )
  endif()
endif()

install(TARGETS EnergyEngine
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Config" "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Shaders" "${CMAKE_CURRENT_SOURCE_DIR}/APPLICATION/Engine/Materials"
  DESTINATION "share/EnergyEngine/Engine"
  OPTIONAL
)
