cmake_minimum_required(VERSION 3.31...4.4.2)

set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON CACHE BOOL "" FORCE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF CACHE BOOL "" FORCE)

project(
    da_elite
    VERSION 0.1.2
    LANGUAGES C CXX RC
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

include(CMakeDependentOption)
include(CTest)
include(FetchContent)

##### Options
option(DA_ELITE_USE_INTERPROCEDURAL_OPTIMIZATION "Enable FLTO/LTCG" ON)
cmake_dependent_option(DA_ELITE_USE_STATIC_MSVCRT
    "Use the static release/debug MSVCRT - or set the flag yourself before including the project" ON
    "MSVC" OFF
)

macro(set_tracked_cache_version VAR_NAME DEFAULT_VAL DOCSTRING)
    if(NOT "${${VAR_NAME}_LAST_DEFAULT}" STREQUAL "${DEFAULT_VAL}")
        set(${VAR_NAME} "${DEFAULT_VAL}" CACHE STRING "${DOCSTRING}" FORCE)
        set(${VAR_NAME}_LAST_DEFAULT "${DEFAULT_VAL}" CACHE INTERNAL "Tracks default change for ${VAR_NAME}")
    else()
        set(${VAR_NAME} "${DEFAULT_VAL}" CACHE STRING "${DOCSTRING}")
    endif()
endmacro()

set_tracked_cache_version(DA_ELITE_USYLIBPP_VERSION "main" "usylibpp git tag/branch")
set_tracked_cache_version(DA_ELITE_GLAZE_VERSION "v9.1.0" "glaze git tag")

# Status Reports
if(WIN32)
    message(STATUS "[${PROJECT_NAME}] Using static MSVCRT: ${DA_ELITE_USE_STATIC_MSVCRT} (DA_ELITE_USE_STATIC_MSVCRT)")
endif()
message(STATUS "[${PROJECT_NAME}] Using interprocedural optimization: ${DA_ELITE_USE_INTERPROCEDURAL_OPTIMIZATION} (DA_ELITE_USE_INTERPROCEDURAL_OPTIMIZATION)")

if(DA_ELITE_USE_STATIC_MSVCRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(DA_ELITE_USE_INTERPROCEDURAL_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set(DA_ELITE_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${DA_ELITE_BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DA_ELITE_BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DA_ELITE_BIN_DIR}")

##### FetchContent dependencies
set(USYLIBPP_ENABLE_WINDOWS ON CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_WINDOWS_UNICODE ON CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_WIL ON CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_WINDOWS_IMAGING OFF CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_TASK_DIALOG_DARK_MODE ON CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_WINTOAST OFF CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_VOIDTOOLS_EVERYTHING OFF CACHE BOOL "" FORCE)
set(USYLIBPP_ENABLE_VOIDTOOLS_EVERYTHING3 OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    usylibpp
    GIT_REPOSITORY https://github.com/usyless/usylibpp.git
    GIT_TAG "${DA_ELITE_USYLIBPP_VERSION}"
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG "${DA_ELITE_GLAZE_VERSION}"
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(usylibpp glaze)

##### Common compile settings
foreach(_part IN ITEMS MAJOR MINOR PATCH TWEAK)
    if(DEFINED PROJECT_VERSION_${_part} AND NOT "${PROJECT_VERSION_${_part}}" STREQUAL "")
        set(DA_ELITE_VERSION_${_part} "${PROJECT_VERSION_${_part}}")
    else()
        set(DA_ELITE_VERSION_${_part} 0)
    endif()
endforeach()
set(DA_ELITE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file("${PROJECT_SOURCE_DIR}/resources/version.h.in" "${DA_ELITE_GENERATED_DIR}/da_elite_version.h" @ONLY)

add_library(da_elite_common INTERFACE)
target_include_directories(da_elite_common INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/resources"
    "${DA_ELITE_GENERATED_DIR}"
)
target_link_libraries(da_elite_common INTERFACE usylibpp::usylibpp glaze::glaze)
target_compile_definitions(da_elite_common INTERFACE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Release>:NDEBUG>
)
if(MSVC)
    target_compile_options(da_elite_common INTERFACE
        /EHsc
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /bigobj
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Gy /Gw>
    )
    target_link_options(da_elite_common INTERFACE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
    )
endif()

##### da-elite.exe - lightweight tray service + Win32 config dialog + CLI
add_executable(da-elite WIN32
    src/main.cpp
    resources/app.rc
)
set_target_properties(da-elite PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(da-elite PRIVATE
    da_elite_common
    hid
    setupapi
    shell32
    user32
    gdi32
    comctl32
    uxtheme
    dwmapi
    advapi32
    ole32
)

##### Unit tests (protocol & settings)
if(BUILD_TESTING)
    add_executable(da-elite-tests
        tests/protocol_tests.cpp
    )
    set_target_properties(da-elite-tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_link_libraries(da-elite-tests PRIVATE
        da_elite_common
        shell32
        user32
        advapi32
        ole32
    )
    add_test(NAME protocol_and_settings COMMAND da-elite-tests)
endif()
