/// @file
/// @brief The simplest possible program: metrics with only the NullAdapter.
///        Nothing is exported, nothing throws — a safe baseline.

#include <prom/prom.hpp>

#include <iostream>

int main() {
    // No Registry to construct or pass around: the free helpers delegate to the
    // process-wide Registry::global(), which here resolves to the NullAdapter.
    auto requests = prom::counter({.name = "demo_requests_total", .help = "demo"});
    auto in_flight = prom::gauge({.name = "demo_in_flight", .help = "demo"});

    requests.inc();
    requests.inc(10);
    in_flight.set(3);
    in_flight.dec();

    std::cout << "backend = " << prom::Registry::global()->adapter()->backend_name() << "\n";
    std::cout << "All operations were safe no-ops.\n";
    return 0;
}
