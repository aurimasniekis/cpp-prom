cmake_minimum_required(VERSION 3.25)

project(prom
    VERSION     0.1.0
    DESCRIPTION "Client-independent C++23 Prometheus/OpenMetrics metric abstraction"
    LANGUAGES   CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(PROM_BUILD_TESTS         "Build prom tests"                       ${PROJECT_IS_TOP_LEVEL})
option(PROM_BUILD_EXAMPLES      "Build prom examples"                    ${PROJECT_IS_TOP_LEVEL})
option(PROM_BUILD_DOCS          "Build Doxygen API documentation"        OFF)
option(PROM_ENABLE_CLANG_TIDY   "Run clang-tidy during build"            OFF)
option(PROM_ENABLE_SANITIZERS   "Build with ASan + UBSan (Debug)"        OFF)
option(PROM_ENABLE_COVERAGE     "Build with Clang source-based coverage" OFF)
option(PROM_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"      ${PROJECT_IS_TOP_LEVEL})
option(PROM_INSTALL             "Generate install rules for prom"        ${PROJECT_IS_TOP_LEVEL})
option(PROM_WITH_PROMETHEUS_CPP "Build the prometheus-cpp backend adapter" OFF)

include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(Dependencies)

# Generate prom/version.hpp from the project version (single source of truth).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/prom/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/prom/version.hpp"
    @ONLY
)

add_library(prom INTERFACE)
add_library(prom::prom ALIAS prom)

target_include_directories(prom
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(prom INTERFACE cxx_std_23)

# commons (display metadata), logman (logging), and dimval (dimensional values)
# are all hard dependencies of the core API.
target_link_libraries(prom
    INTERFACE
        commons::commons
        logman::logman
        dimval::dimval
)

if(PROM_ENABLE_CLANG_TIDY)
    find_program(PROM_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(_prom_tidy_cmd
        "${PROM_CLANG_TIDY_EXE}"
        "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
    )
    # clang-tidy runs clang's frontend, which does its OWN libstdc++ detection
    # independent of CMAKE_CXX_COMPILER. With multiple gcc versions installed it
    # can land on the wrong one. Pin tidy to the same gcc install the real
    # compile uses.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-search-dirs
            OUTPUT_VARIABLE _prom_gcc_search_dirs
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_prom_gcc_search_dirs MATCHES "install: ([^\n]+)")
            list(APPEND _prom_tidy_cmd
                "--extra-arg-before=--gcc-install-dir=${CMAKE_MATCH_1}")
            message(STATUS
                "prom: pinning clang-tidy to gcc install dir ${CMAKE_MATCH_1}")
        endif()
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${_prom_tidy_cmd}")
    message(STATUS "prom: clang-tidy enabled (${PROM_CLANG_TIDY_EXE})")
endif()

# Decide install/export viability up front — before the tests/examples/adapters
# subdirectories, which each consult PROM_INSTALL. Disable it when a dependency
# arrived via FetchContent and cannot be re-exported (mirrors plugs / dimval).
if(PROM_INSTALL AND (DEFINED commons_SOURCE_DIR OR DEFINED logman_SOURCE_DIR
                     OR DEFINED dimval_SOURCE_DIR OR DEFINED spdlog_SOURCE_DIR))
    message(STATUS
        "prom: PROM_INSTALL disabled — a dependency (commons, logman, dimval, "
        "and/or spdlog) was built via FetchContent and cannot be re-exported. "
        "Install the dependencies from system packages (or use find_package) to "
        "re-enable prom's install rules."
    )
    set(PROM_INSTALL OFF)
endif()

if(PROM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PROM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Optional backend adapters (prometheus-cpp, ...). Each is gated by its own
# PROM_WITH_* option, so a disabled adapter is never parsed and its
# dependencies are never fetched.
add_subdirectory(adapters)

if(PROM_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(PROM_DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(prom_docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
endif()

if(PROM_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS prom
        EXPORT  promTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/prom
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/include/prom/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/prom
    )

    install(EXPORT promTargets
        FILE        promTargets.cmake
        NAMESPACE   prom::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/promConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/promConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/promConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/promConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/promConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom
    )
endif()
