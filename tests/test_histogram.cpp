/// @file
/// @brief Histogram behavior: observe, default buckets, dimensional values.

#include <prom/histogram.hpp>
#include <prom/registry.hpp>

#include <dimval/dimval.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Histogram;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class HistogramTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(HistogramTest, ObserveRecordsEachSample) {
    auto hist = registry->histogram({.name = "latency", .help = "h"});
    hist.observe(0.1);
    hist.observe(0.2);
    hist.observe(1.5);
    EXPECT_EQ(backend->family("latency")->count("observe"), 3U);
}

TEST_F(HistogramTest, DefaultBucketsWhenUnspecified) {
    auto hist = registry->histogram({.name = "latency", .help = "h"});
    EXPECT_FALSE(prom::default_histogram_buckets.empty());
    EXPECT_EQ(prom::default_histogram_buckets.front(), 0.005);
    // Default buckets are applied by the registry; the metric still works.
    hist.observe(0.05);
    EXPECT_EQ(backend->family("latency")->count("observe"), 1U);
}

TEST_F(HistogramTest, AcceptsDimensionalValue) {
    auto hist = registry->histogram({.name = "delays", .help = "h"});
    hist.observe(dimval::SecondValue{0.25});
    EXPECT_DOUBLE_EQ(backend->family("delays")->total("observe"), 0.25);
}
