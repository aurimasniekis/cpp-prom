include_guard(GLOBAL)
include(FetchContent)

# prometheus-cpp — the real Prometheus client. Only pulled in when
# PROM_WITH_PROMETHEUS_CPP is ON (the adapters/ subdirectory includes this).
# The adapter itself needs only the in-process `core` library. The `pull`
# library (Civetweb HTTP exposer) is built **only when tests are enabled**, for
# the integration test that scrapes /metrics over a real socket — so a plain
# adapter build stays lean and never compiles civetweb. Push gateway and the
# upstream test suite stay off either way.
set(ENABLE_PUSH       OFF CACHE INTERNAL "")
set(ENABLE_COMPRESSION OFF CACHE INTERNAL "")
set(ENABLE_TESTING    OFF CACHE INTERNAL "")
set(USE_THIRDPARTY_LIBRARIES OFF CACHE INTERNAL "")
set(OVERRIDE_CXX_STANDARD_FLAGS OFF CACHE INTERNAL "")

if(PROM_BUILD_TESTS)
    set(ENABLE_PULL ON CACHE INTERNAL "")

    # civetweb — the HTTP server behind prometheus-cpp's pull Exposer. We fetch
    # it ourselves because prometheus-cpp's release tarball ships no bundled
    # civetweb (its 3rdparty submodules are empty in the archive). Fetched
    # BEFORE prometheus-cpp so CMake's FetchContent redirect satisfies
    # prometheus-cpp's `find_package(civetweb CONFIG REQUIRED)`. Server
    # executable, SSL, and tests are all off — we only need the C++ HTTP server.
    set(CIVETWEB_ENABLE_CXX               ON  CACHE INTERNAL "")
    set(CIVETWEB_BUILD_TESTING            OFF CACHE INTERNAL "")
    set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE INTERNAL "")
    set(CIVETWEB_ENABLE_SSL               OFF CACHE INTERNAL "")
    set(CIVETWEB_ENABLE_SSL_DYNAMIC_LOADING OFF CACHE INTERNAL "")
    set(CIVETWEB_INSTALL_EXECUTABLE       OFF CACHE INTERNAL "")
    set(CIVETWEB_ENABLE_ASAN              OFF CACHE INTERNAL "")
    FetchContent_Declare(
        civetweb
        URL      https://github.com/civetweb/civetweb/archive/refs/tags/v1.16.tar.gz
        URL_HASH SHA256=f0e471c1bf4e7804a6cfb41ea9d13e7d623b2bcc7bc1e2a4dd54951a24d60285
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES civetweb
    )
    # civetweb 1.16 still declares cmake_minimum_required(VERSION 3.1), which
    # newer CMake refuses. Lift the policy floor just for it, then restore.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(civetweb)
    unset(CMAKE_POLICY_VERSION_MINIMUM)

    # prometheus-cpp's pull links the namespaced `civetweb::civetweb-cpp`
    # target, which civetweb only exports from its *installed* config — the
    # in-tree build defines the bare target names. Bridge the namespaced
    # spellings to whatever civetweb created so the find_package consumer links.
    foreach(_cw_cpp IN ITEMS civetweb-cpp civetweb-cpp-library)
        if(TARGET ${_cw_cpp} AND NOT TARGET civetweb::civetweb-cpp)
            add_library(civetweb::civetweb-cpp ALIAS ${_cw_cpp})
        endif()
    endforeach()
    foreach(_cw_c IN ITEMS civetweb-c-library civetweb)
        if(TARGET ${_cw_c} AND NOT TARGET civetweb::civetweb-c-library)
            add_library(civetweb::civetweb-c-library ALIAS ${_cw_c})
        endif()
    endforeach()
else()
    set(ENABLE_PULL OFF CACHE INTERNAL "")
endif()

FetchContent_Declare(
    prometheus-cpp
    URL      https://github.com/jupp0r/prometheus-cpp/archive/refs/tags/v1.3.0.tar.gz
    URL_HASH SHA256=ac6e958405a29fbbea9db70b00fa3c420e16ad32e1baf941ab233ba031dd72ee
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS NAMES prometheus-cpp
)
FetchContent_MakeAvailable(prometheus-cpp)
