include_guard(GLOBAL)

# prom_enable_coverage(<target>)
#
# Adds Clang source-based coverage flags to <target> when PROM_ENABLE_COVERAGE
# is ON. No-op on MSVC.
function(prom_enable_coverage target)
    if(NOT PROM_ENABLE_COVERAGE)
        return()
    endif()
    if(MSVC)
        message(STATUS "prom: coverage requested but skipped on MSVC")
        return()
    endif()

    set(_cov_flags -fprofile-instr-generate -fcoverage-mapping)
    target_compile_options(${target} PRIVATE ${_cov_flags} -O0 -g)
    target_link_options   (${target} PRIVATE ${_cov_flags})
endfunction()
