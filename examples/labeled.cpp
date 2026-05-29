/// @file
/// @brief Labeled child series: one family, many label combinations.

#include <prom/prom.hpp>

#include <iostream>

int main() {
    const auto registry = prom::Registry::create();

    // Constant labels apply to the whole family; dynamic labels select a child.
    const auto requests = registry->counter({.name = "http_requests_total",
                                             .help = "HTTP requests",
                                             .labels = prom::Labels{{"service", "api"}}});

    requests.labels(prom::Labels{{"method", "GET"}, {"code", "200"}}).inc();
    requests.labels(prom::Labels{{"method", "GET"}, {"code", "200"}}).inc();
    requests.labels(prom::Labels{{"method", "POST"}, {"code", "500"}}).inc();

    std::cout << "Recorded labeled requests via the " << registry->adapter()->backend_name()
              << " backend\n";
    return 0;
}
