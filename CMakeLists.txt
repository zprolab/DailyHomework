cmake_minimum_required(VERSION 3.21)

project(DaHoWork VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DHW_BUILD_QT "Build the Qt 6 desktop shell and the PNG export tool" ON)
option(DHW_BUILD_TOOLS "Build command line tools" ON)
option(DHW_BUILD_TESTS "Build the unit tests" ON)
option(DHW_STATIC_QT "Static MSVC runtime, paired with a static Qt build" OFF)
# Opt-in WinUI 3 / Fluent widget skin, built from the tree vendored in
# third_party/fluentqt (MIT). OFF keeps the stock platform style and does not
# configure, compile or link the vendored library at all.
option(DHW_UI_FLUENT "Use the vendored Fluent-Qt widget library" OFF)

# A static Qt on Windows is usually configured with -static-runtime, so the
# application objects must use the same runtime library.
if(DHW_STATIC_QT AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

function(dhw_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
        # std::fopen / _wgetenv are used for their portable signatures; the "secure"
        # replacements are MSVC specific and would fork the code for no benefit.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endfunction()

add_subdirectory(core)

if(DHW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DHW_BUILD_QT)
    find_package(Qt6 6.4 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_subdirectory(render)
        if(DHW_UI_FLUENT)
            if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fluentqt/CMakeLists.txt")
                message(FATAL_ERROR
                    "DHW_UI_FLUENT=ON but third_party/fluentqt/CMakeLists.txt is missing")
            endif()
            # The upstream defaults for every optional module are "${FLUENT_QT_IS_TOP_LEVEL}",
            # so being a subdirectory already turns the Gallery, the examples, the install
            # rules and the upstream test suite off. They are pinned here anyway so that a
            # future upstream default cannot silently pull in spdlog or gtest.
            set(FLUENT_QT_BUILD_EXAMPLES OFF CACHE BOOL "Fluent-Qt examples" FORCE)
            set(FLUENT_QT_BUILD_GALLERY OFF CACHE BOOL "Fluent-Qt Gallery application" FORCE)
            set(FLUENT_QT_BUILD_TESTS OFF CACHE BOOL "Fluent-Qt test targets" FORCE)
            set(FLUENT_QT_BUILD_SPATIAL OFF CACHE BOOL "Fluent-Qt Spatial module" FORCE)
            set(FLUENT_QT_BUILD_QML_BRIDGE OFF CACHE BOOL "Fluent-Qt QML bridge" FORCE)
            set(FLUENT_QT_BUILD_PYSIDE6_BINDINGS OFF CACHE BOOL "Fluent-Qt PySide6 bindings" FORCE)
            set(FLUENT_QT_INSTALL OFF CACHE BOOL "Fluent-Qt install rules" FORCE)
            add_subdirectory(third_party/fluentqt)
            if(TARGET FluentQt AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                # Qt 6.12 deprecated the Q_OS_MAC macro and Fluent-Qt 1.10.1 still uses it
                # inside its platform compatibility shims. Silence exactly that diagnostic
                # for the vendored target instead of patching upstream sources.
                target_compile_options(FluentQt PRIVATE -Wno-deprecated-pragma)
            endif()
        endif()
        if(DHW_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
            add_subdirectory(tools)
        endif()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/qt/CMakeLists.txt")
            add_subdirectory(apps/qt)
        endif()
    else()
        message(STATUS "Qt6 Widgets not found - the desktop shell and the PNG export tool are skipped")
    endif()
endif()
