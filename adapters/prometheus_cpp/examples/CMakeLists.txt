add_executable(prom_prometheus_backend prometheus_backend.cpp)
target_link_libraries(prom_prometheus_backend PRIVATE prom::prometheus_cpp)

if(TARGET prometheus-cpp::core)
    target_link_libraries(prom_prometheus_backend PRIVATE prometheus-cpp::core)
elseif(TARGET core)
    target_link_libraries(prom_prometheus_backend PRIVATE core)
endif()

prom_set_warnings(prom_prometheus_backend PRIVATE)
prom_enable_sanitizers(prom_prometheus_backend)
set_target_properties(prom_prometheus_backend PROPERTIES CXX_CLANG_TIDY "")
