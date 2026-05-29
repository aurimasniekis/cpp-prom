/// @file
/// @brief A library declaring and using metrics with no knowledge of the
///        backend. This is the headline use case for prom: ship metric
///        instrumentation in a library without depending on a concrete client.

#include <prom/prom.hpp>

#include <iostream>

// --- somewhere deep inside a reusable library -------------------------------
namespace mylib {

// The library doesn't hold a registry at all: the free helpers create metrics
// through the process-wide Registry::global(), which resolves to whatever
// adapter the host installed (NullAdapter if none).
class Telemetry {
public:
    static Telemetry& instance() {
        static Telemetry telemetry;
        return telemetry;
    }

    prom::Counter requests =
        prom::counter({.name = "mylib_requests_total", .help = "Total requests handled by mylib"});
    prom::Histogram latency =
        prom::histogram({.name = "mylib_request_seconds", .help = "Request latency in seconds"});
};

void handle_request(const double seconds) {
    Telemetry::instance().requests.inc();
    Telemetry::instance().latency.observe(seconds);
}

}  // namespace mylib

int main() {
    // The application never installed a backend, so everything is a logged
    // no-op via NullAdapter — yet the library code runs unchanged.
    std::cout << "Running mylib with the default (null) backend\n";
    mylib::handle_request(0.012);
    mylib::handle_request(0.044);
    mylib::handle_request(0.131);
    std::cout << "Handled 3 requests; metrics went to "
              << prom::Registry::global()->adapter()->backend_name() << " backend\n";
    return 0;
}
