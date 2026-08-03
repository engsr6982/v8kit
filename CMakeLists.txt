cmake_minimum_required(VERSION 3.20)

if(WIN32)
    cmake_policy(SET CMP0091 NEW)
endif()

project(jspp VERSION 2.0.0 LANGUAGES CXX)

# ==============================================================================
# Options
# ==============================================================================
set(ALLOWED_BACKENDS quickjs v8 stub)
set(JSPP_BACKEND "v8" CACHE STRING "Select JS backend: v8 or quickjs")

set(JSPP_EXTERNAL_INC "" CACHE PATH "Optional: Path to JS engine headers")
set(JSPP_EXTERNAL_LIB "" CACHE PATH "Optional: Path to JS engine library files")

message(STATUS "[jspp] Backend: ${JSPP_BACKEND}")
message(STATUS "[jspp] External Inc: ${JSPP_EXTERNAL_INC}")
message(STATUS "[jspp] External Lib: ${JSPP_EXTERNAL_LIB}")

if(NOT ${JSPP_BACKEND} IN_LIST ALLOWED_BACKENDS)
    message(FATAL_ERROR "Backend '${JSPP_BACKEND}' is not valid. Must be one of: ${ALLOWED_BACKENDS}")
endif()


# ==============================================================================
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ==============================================================================
# Target: jspp
# ==============================================================================
add_library(jspp STATIC)

file(GLOB_RECURSE CORE_SRC "src/*.cc")
target_sources(jspp PRIVATE ${CORE_SRC})
target_include_directories(jspp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    "$<INSTALL_INTERFACE:include>")


target_compile_definitions(jspp PUBLIC "$<$<CONFIG:Debug>:JSPP_DEBUG>")

if(JSPP_EXTERNAL_INC)
    target_include_directories(jspp PUBLIC ${JSPP_EXTERNAL_INC})
endif()
if(JSPP_EXTERNAL_LIB)
    target_link_libraries(jspp PUBLIC ${JSPP_EXTERNAL_LIB})
endif()

set(BACKEND_CONFIGURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src-${JSPP_BACKEND}/Configure.cmake")
if (EXISTS ${BACKEND_CONFIGURE_FILE})
    include(${BACKEND_CONFIGURE_FILE})
else()
    message(FATAL_ERROR "Backend configuration file not found: ${BACKEND_CONFIGURE_FILE}")
endif()


if(WIN32)
    target_compile_options(jspp PUBLIC /utf-8 /W4 /sdl /Zc:__cplusplus /Zc:preprocessor)
elseif(UNIX AND NOT APPLE)
    set_property(TARGET jspp PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_compile_options(jspp PUBLIC -Wall -Wextra)

    if(UNIX AND NOT APPLE)
        target_link_libraries(jspp PUBLIC dl pthread)
    endif()
endif()


# ==============================================================================
# Target: test
# ==============================================================================
option(JSPP_BUILD_TESTS "Build test cases" OFF)
if(JSPP_BUILD_TESTS)
    add_subdirectory(test)
endif()


# ==============================================================================
# Install / Export
# ==============================================================================
include(CMakePackageConfigHelpers)

# 安装头文件：核心 (src/jspp) + 所选后端的实现头文件 (src-<backend>/jspp-backend)
install(DIRECTORY src/jspp DESTINATION include PATTERN "*.cc" EXCLUDE)
install(DIRECTORY src-${JSPP_BACKEND}/jspp-backend DESTINATION include PATTERN "*.cc" EXCLUDE)

# 导出目标，PUBLIC 宏 (JSPP_BACKEND_XXX, JSPP_DEBUG) 与 include 目录会随导出文件携带
install(TARGETS jspp
        EXPORT jsppTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(EXPORT jsppTargets
        FILE jsppTargets.cmake
        NAMESPACE jspp::
        DESTINATION lib/cmake/jspp)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/jsppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/jsppConfig.cmake"
    INSTALL_DESTINATION lib/cmake/jspp)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/jsppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/jsppConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/jsppConfigVersion.cmake"
        DESTINATION lib/cmake/jspp)