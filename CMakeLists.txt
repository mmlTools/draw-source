cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# ---------------------------------------------------------------------------
# Project metadata
# ---------------------------------------------------------------------------
if(NOT DEFINED _name)
  set(_name "instant-highlight-source-draw")
endif()

if(NOT DEFINED _version)
  set(_version "1.0.0")
endif()

project(${_name} VERSION ${_version} LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ENABLE_FRONTEND_API "Use obs-frontend-api for dock UI integration" ON)
option(ENABLE_QT "Use Qt for dock UI" ON)
option(USE_SYSTEM_OBS "Use an installed OBS SDK instead of downloading dependencies" OFF)
option(BUILD_TESTING "Build drawing regression tests" OFF)

# This plugin relies on Qt + frontend API for the dock. Keep the build strict
# and fail early when misconfigured, similar to Smart Lower Thirds.
if(NOT ENABLE_QT)
  message(FATAL_ERROR "ENABLE_QT=OFF is not supported for ${_name}; Qt is required for the dock.")
endif()

if(NOT ENABLE_FRONTEND_API)
  message(
    FATAL_ERROR
    "ENABLE_FRONTEND_API=OFF is not supported for ${_name}; obs-frontend-api is required for the dock."
  )
endif()

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# OBS 31/32+: dock-by-id API (preferred)
add_compile_definitions(HAVE_OBS_DOCK_BY_ID=1)

# ---------------------------------------------------------------------------
# Target (OBS plugin)
# ---------------------------------------------------------------------------
add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ENABLE_FRONTEND_API=1 ENABLE_QT=1 PLUGIN_NAME_STR="${_name}" PLUGIN_VERSION_STR="${_version}"
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/draw_source.cpp
    src/draw_source.hpp
    src/draw_dock.cpp
    src/draw_dock.hpp
    src/drawing_document.cpp
    src/drawing_document.hpp
    src/preview_overlay.cpp
    src/preview_overlay.hpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME ${_name}
)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
