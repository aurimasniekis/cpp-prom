include(GoogleTest)

add_executable(prom_prometheus_cpp_tests test_prometheus_cpp.cpp)

target_link_libraries(prom_prometheus_cpp_tests
    PRIVATE
        prom::prometheus_cpp
        GTest::gtest_main
)

# This test reaches into prometheus-cpp directly (Registry, TextSerializer) to
# assert on the rendered scrape output, so it needs the core library's headers.
if(TARGET prometheus-cpp::core)
    target_link_libraries(prom_prometheus_cpp_tests PRIVATE prometheus-cpp::core)
elseif(TARGET core)
    target_link_libraries(prom_prometheus_cpp_tests PRIVATE core)
endif()

prom_set_warnings(prom_prometheus_cpp_tests PRIVATE)
prom_enable_sanitizers(prom_prometheus_cpp_tests)
set_target_properties(prom_prometheus_cpp_tests PROPERTIES CXX_CLANG_TIDY "")

gtest_discover_tests(prom_prometheus_cpp_tests)

# --- HTTP-level integration test ------------------------------------------
#
# Serves metrics over a real prometheus::Exposer (the Civetweb pull endpoint)
# and scrapes /metrics over an actual socket. Needs the pull library on top of
# core; both target spellings are handled (FetchContent vs find_package).
if(TARGET prometheus-cpp::pull)
    set(_prom_pcpp_pull prometheus-cpp::pull)
elseif(TARGET pull)
    set(_prom_pcpp_pull pull)
endif()

if(DEFINED _prom_pcpp_pull)
    add_executable(prom_prometheus_cpp_integration test_prometheus_cpp_integration.cpp)

    target_link_libraries(prom_prometheus_cpp_integration
        PRIVATE
            prom::prometheus_cpp
            GTest::gtest_main
            ${_prom_pcpp_pull}
    )
    if(TARGET prometheus-cpp::core)
        target_link_libraries(prom_prometheus_cpp_integration PRIVATE prometheus-cpp::core)
    elseif(TARGET core)
        target_link_libraries(prom_prometheus_cpp_integration PRIVATE core)
    endif()

    prom_set_warnings(prom_prometheus_cpp_integration PRIVATE)
    prom_enable_sanitizers(prom_prometheus_cpp_integration)
    set_target_properties(prom_prometheus_cpp_integration PROPERTIES CXX_CLANG_TIDY "")

    gtest_discover_tests(prom_prometheus_cpp_integration)
else()
    message(WARNING
        "prom: prometheus-cpp pull target not found — skipping the HTTP "
        "integration test. Build with the bundled prometheus-cpp (ENABLE_PULL=ON) "
        "to enable it.")
endif()
