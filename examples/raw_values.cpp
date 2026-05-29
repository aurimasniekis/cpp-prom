/// @file
/// @brief Recording plain arithmetic values — the simplest path through the API.

#include <prom/prom.hpp>

#include <iostream>

int main() {
    const auto registry = prom::Registry::create();

    auto bytes = registry->counter({.name = "bytes_processed_total", .help = "bytes"});
    auto temp = registry->gauge({.name = "cpu_celsius", .help = "temperature"});

    // Integers, doubles, floats — all arithmetic types route through normalize().
    bytes.inc(512);
    bytes.inc(1024U);
    temp.set(41.7);
    temp.set(39);

    std::cout << "Recorded raw arithmetic samples via the " << registry->adapter()->backend_name()
              << " backend\n";
    return 0;
}
