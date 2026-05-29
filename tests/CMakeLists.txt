include(GoogleTest)

add_executable(prom_tests
    test_labels.cpp
    test_null_adapter.cpp
    test_composite_adapter.cpp
    test_global.cpp
    test_global_registry.cpp
    test_global_decoration.cpp
    test_registry.cpp
    test_scope.cpp
    test_counter.cpp
    test_gauge.cpp
    test_histogram.cpp
    test_summary.cpp
    test_untyped.cpp
    test_info.cpp
    test_stateset.cpp
    test_dimval.cpp
    test_standalone.cpp
    test_migration.cpp
    test_enumeration.cpp
)

target_link_libraries(prom_tests
    PRIVATE
        prom::prom
        GTest::gtest_main
)

prom_set_warnings(prom_tests PRIVATE)
prom_enable_sanitizers(prom_tests)
prom_enable_coverage(prom_tests)

gtest_discover_tests(prom_tests)
