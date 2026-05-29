/// @file
/// @brief Gauge behavior: set/inc/dec, +/-1 helpers, raw and dimensional values.

#include <prom/registry.hpp>

#include <dimval/dimval.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Gauge;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class GaugeTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(GaugeTest, SetIncDec) {
    auto gauge = registry->gauge({.name = "g", .help = "h"});
    gauge.set(10.0);
    gauge.inc();
    gauge.inc(4.0);
    gauge.dec();
    gauge.dec(2.0);

    const auto family = backend->family("g");
    EXPECT_DOUBLE_EQ(family->total("set"), 10.0);
    EXPECT_DOUBLE_EQ(family->total("inc"), 5.0);
    EXPECT_DOUBLE_EQ(family->total("dec"), 3.0);
}

TEST_F(GaugeTest, AcceptsNegativeSet) {
    auto gauge = registry->gauge({.name = "temp", .help = "h"});
    gauge.set(-12.5);
    EXPECT_DOUBLE_EQ(backend->family("temp")->total("set"), -12.5);
}

TEST_F(GaugeTest, AcceptsDimensionalValue) {
    auto gauge = registry->gauge({.name = "freq", .help = "h"});
    gauge.set(dimval::HertzValue{440.0});
    EXPECT_DOUBLE_EQ(backend->family("freq")->total("set"), 440.0);
}
