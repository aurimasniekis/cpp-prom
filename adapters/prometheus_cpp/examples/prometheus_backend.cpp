/// @file
/// @brief Install the prometheus-cpp backend as the process default, record a
///        few samples through prom's client-independent API, and print the
///        scrape text a `prometheus::Exposer` would serve at `/metrics`.

#include <prom/prom.hpp>
#include <prom/prometheus_cpp/adapter.hpp>

#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <iostream>
#include <memory>

int main() {
    // 1. The host application installs a real backend, once, at startup.
    const auto adapter = std::make_shared<prom::prometheus_cpp::PrometheusCppAdapter>();
    prom::Registry::global()->set_adapter(adapter);

    // 2. Library code uses prom exactly as it would with the null backend.
    const auto registry = prom::Registry::global();
    const auto requests = registry->counter({.name = "http_requests_total",
                                             .help = "Total HTTP requests",
                                             .labels = prom::Labels{{"service", "api"}}});
    auto in_flight = registry->gauge({.name = "http_in_flight", .help = "In-flight requests"});
    auto latency = registry->histogram(
        {.name = "http_request_seconds", .help = "Latency", .buckets = {0.05, 0.1, 0.25, 0.5, 1}});

    requests.inc();
    requests.labels(prom::Labels{{"code", "200"}}).inc(3);
    in_flight.set(2);
    in_flight.dec();
    latency.observe(0.08);
    latency.observe(0.42);

    // 3. The host exposes adapter->registry() through an HTTP endpoint. Here we
    //    just render what a scrape would return.
    std::cout << "backend = " << adapter->backend_name() << "\n\n";
    std::cout << ::prometheus::TextSerializer().Serialize(adapter->registry().Collect());

    prom::Registry::global()->set_adapter(nullptr);
    return 0;
}
