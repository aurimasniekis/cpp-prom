include_guard(GLOBAL)

# prom_enable_sanitizers(<target>)
#
# Adds AddressSanitizer + UndefinedBehaviorSanitizer flags to <target> when
# PROM_ENABLE_SANITIZERS is ON and the toolchain is GCC or Clang.
function(prom_enable_sanitizers target)
    if(NOT PROM_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(STATUS "prom: sanitizers requested but skipped on MSVC")
        return()
    endif()

    set(_san_flags
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )

    target_compile_options(${target} PRIVATE ${_san_flags})
    target_link_options   (${target} PRIVATE ${_san_flags})
endfunction()
