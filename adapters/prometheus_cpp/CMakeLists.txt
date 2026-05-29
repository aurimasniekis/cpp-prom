# prom::prometheus_cpp — opt-in prometheus-cpp backend adapter.
# Only included from adapters/CMakeLists.txt when PROM_WITH_PROMETHEUS_CPP=ON.

include("${CMAKE_SOURCE_DIR}/cmake/PrometheusCppDependencies.cmake")

add_library(prom_prometheus_cpp STATIC
    src/adapter.cpp
)
add_library(prom::prometheus_cpp ALIAS prom_prometheus_cpp)

target_compile_features(prom_prometheus_cpp PUBLIC cxx_std_23)
target_include_directories(prom_prometheus_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# prometheus-cpp exposes its in-process library as `prometheus-cpp::core` both
# via FetchContent and find_package; fall back to the bare `core` target name.
if(TARGET prometheus-cpp::core)
    set(_prom_pcpp_lib prometheus-cpp::core)
elseif(TARGET core)
    set(_prom_pcpp_lib core)
else()
    message(FATAL_ERROR "prom: no prometheus-cpp core target found")
endif()

target_link_libraries(prom_prometheus_cpp
    PUBLIC  prom::prom
    PRIVATE ${_prom_pcpp_lib}
)

prom_set_warnings(prom_prometheus_cpp PRIVATE)
prom_enable_sanitizers(prom_prometheus_cpp)
prom_enable_coverage(prom_prometheus_cpp)

# Keep clang-tidy off this target: it links prometheus-cpp, whose headers do not
# satisfy our config. The core headers are linted via the tests/examples.
set_target_properties(prom_prometheus_cpp PROPERTIES CXX_CLANG_TIDY "")

if(PROM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(PROM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(PROM_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS prom_prometheus_cpp
        EXPORT  prom-prometheus-cppTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/prom/prometheus_cpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/prom
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT prom-prometheus-cppTargets
        FILE        prom-prometheus-cppTargets.cmake
        NAMESPACE   prom::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom-prometheus-cpp
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/prom-prometheus-cppConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/prom-prometheus-cppConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/prom-prometheus-cppConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom-prometheus-cpp
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/prom-prometheus-cppConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/prom-prometheus-cppConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prom-prometheus-cpp
    )
endif()
