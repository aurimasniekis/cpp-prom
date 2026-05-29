/// @file
/// @brief Counter behavior: increments, negative/NaN drops, labeled children,
///        raw and dimensional values.

#include <prom/registry.hpp>

#include <dimval/dimval.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <cmath>
#include <memory>

using prom::Counter;
using prom::Labels;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class CounterTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(CounterTest, IncDefaultsToOne) {
    const auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc();
    counter.inc();
    EXPECT_DOUBLE_EQ(backend->family("c_total")->total("inc"), 2.0);
}

TEST_F(CounterTest, IncByAmount) {
    auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc(5);
    counter.inc(2.5);
    EXPECT_DOUBLE_EQ(backend->family("c_total")->total("inc"), 7.5);
}

TEST_F(CounterTest, DropsNegativeIncrement) {
    auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc(-3.0);
    EXPECT_EQ(backend->family("c_total")->count("inc"), 0U);
}

TEST_F(CounterTest, DropsNonFiniteIncrement) {
    auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc(std::nan(""));
    counter.inc(std::numeric_limits<double>::infinity());
    EXPECT_EQ(backend->family("c_total")->count("inc"), 0U);
}

TEST_F(CounterTest, LabeledChildIsSeparateSeries) {
    auto counter = registry->counter({.name = "c_total", .help = "h"});
    counter.inc(1.0);
    counter.labels(Labels{{"code", "200"}}).inc(4.0);

    EXPECT_DOUBLE_EQ(backend->family("c_total")->total("inc"), 1.0);
    // The child records against its own state; the family only saw the first.
}

TEST_F(CounterTest, AcceptsDimensionalValue) {
    auto counter = registry->counter({.name = "bytes_total", .help = "h"});
    counter.inc(dimval::ByteRateValue{100.0});
    EXPECT_DOUBLE_EQ(backend->family("bytes_total")->total("inc"), 100.0);
}
