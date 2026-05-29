include_guard(GLOBAL)
include(FetchContent)

# All dependencies are resolved as GitHub tag tarballs with a pinned SHA256, and
# every Declare carries FIND_PACKAGE_ARGS so a system/installed copy is used
# instead when available.

# aurimasniekis/cpp-commons — vocabulary types (comms::DisplayInfo / Icon /
# Color). Always required. Declared FIRST so dimval's own internal
# FetchContent_Declare(commons) is a no-op (the first declaration wins),
# avoiding a double fetch.
FetchContent_Declare(
    commons
    URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.4.tar.gz
    URL_HASH SHA256=511268a30e692e82365669738c734d093edc7ca76c898337125f032569c8f907
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1
)
FetchContent_MakeAvailable(commons)

# spdlog — logman links `spdlog::spdlog_header_only`. logman would normally
# fetch spdlog from its own cmake/Dependencies.cmake, but that include() resolves
# back to *this* file via the shared CMAKE_MODULE_PATH + include_guard and
# short-circuits, so we must provide spdlog ourselves (declared before logman).
set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
set(SPDLOG_INSTALL       OFF CACHE INTERNAL "")
set(SPDLOG_FMT_EXTERNAL  OFF CACHE INTERNAL "")
FetchContent_Declare(
    spdlog
    URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 1.17.0
)
FetchContent_MakeAvailable(spdlog)

# aurimasniekis/cpp-logman — channelized logging on top of spdlog. Its tests
# and examples default off when not the top-level project, but be explicit.
set(LOGMAN_BUILD_TESTS    OFF CACHE INTERNAL "")
set(LOGMAN_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(LOGMAN_INSTALL        OFF CACHE INTERNAL "")
FetchContent_Declare(
    logman
    URL      https://github.com/aurimasniekis/cpp-logman/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=443dd2a0928d4bfc24281ad8e9035b302bf68f15e666ce4d6ffbf6ec25c761ed
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.1 NAMES logman
)
FetchContent_MakeAvailable(logman)

# aurimasniekis/cpp-dimval — dimensional value types. The core only matches
# them structurally, so keep dimval lean: no JSON, no parcel. commons is
# already declared above, so dimval's internal commons fetch short-circuits.
set(DIMVAL_BUILD_TESTS        OFF CACHE INTERNAL "")
set(DIMVAL_BUILD_EXAMPLES     OFF CACHE INTERNAL "")
set(DIMVAL_INSTALL            OFF CACHE INTERNAL "")
set(DIMVAL_WITH_NLOHMANN_JSON OFF CACHE BOOL "" FORCE)
set(DIMVAL_WITH_PARCEL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    dimval
    URL      https://github.com/aurimasniekis/cpp-dimval/archive/refs/tags/v0.2.0.tar.gz
    URL_HASH SHA256=7ec1fa93abefc0d56d8ffbffadaecc06f9e2705e7b6aee57befa9c87f73149c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 0.2 NAMES dimval
)
FetchContent_MakeAvailable(dimval)

if(PROM_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK   OFF CACHE INTERNAL "")
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )
    FetchContent_MakeAvailable(googletest)
endif()
