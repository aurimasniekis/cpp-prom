# Iterate optional backend adapters. Each adapter lives in adapters/<name>/ and
# is gated by PROM_WITH_<NAME>=ON. Disabled adapters are never parsed, so their
# dependencies are never fetched.

if(PROM_WITH_PROMETHEUS_CPP)
    add_subdirectory(prometheus_cpp)
endif()
