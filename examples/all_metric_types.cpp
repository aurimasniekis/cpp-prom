/// @file
/// @brief Exercises every metric type prom offers, against the NullAdapter.

#include <prom/prom.hpp>

#include <iostream>

int main() {
    const auto registry = prom::Registry::create();

    auto counter = registry->counter({.name = "events_total", .help = "events"});
    auto gauge = registry->gauge({.name = "queue_depth", .help = "queue depth"});
    auto hist =
        registry->histogram({.name = "op_seconds", .help = "op latency", .buckets = {0.1, 0.5, 1}});
    auto summ = registry->summary({.name = "payload_bytes", .help = "payload sizes"});
    auto unt = registry->untyped({.name = "external_value", .help = "raw"});
    auto info = registry->info({.name = "build_info", .help = "build metadata"});
    auto ss = registry->stateset({.name = "service_state",
                                  .help = "lifecycle",
                                  .states = {"starting", "running", "stopped"}});

    counter.inc();
    counter.inc(7);
    gauge.set(5);
    gauge.dec();
    hist.observe(0.3);
    summ.observe(2048);
    unt.set(-1.5);
    info.set({{"version", "0.1.0"}, {"commit", "deadbeef"}});
    ss.set("running", true);

    std::cout << "Exercised every metric type via the " << registry->adapter()->backend_name()
              << " backend\n";
    return 0;
}
