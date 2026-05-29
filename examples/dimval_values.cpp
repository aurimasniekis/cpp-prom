/// @file
/// @brief Recording dimensional (dimval) values: the unit and its kind ride
///        along with the magnitude, and prom infers the metric's unit from the
///        first observation.

#include <prom/prom.hpp>

#include <dimval/dimval.hpp>

#include <iostream>

int main() {
    const auto registry = prom::Registry::create();

    auto latency = registry->histogram({.name = "rpc_seconds", .help = "RPC latency"});
    auto throughput = registry->gauge({.name = "link_byte_rate", .help = "link throughput"});
    auto tuned = registry->gauge({.name = "radio_center_hz", .help = "tuned frequency"});

    // dimval values carry their unit + dimensional kind; prom latches the unit
    // from the first sample and rejects later samples of an incompatible kind.
    latency.observe(dimval::SecondValue{0.0123});
    throughput.set(dimval::ByteRateValue{1.25e6});
    tuned.set(dimval::CenterFrequencyValue{100.3e6});

    std::cout << "Recorded dimensional samples (Latency, ByteRate, CenterFrequency) via the "
              << registry->adapter()->backend_name() << " backend\n";
    return 0;
}
