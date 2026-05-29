/// @file
/// @brief Summary behavior: observe and default quantiles.

#include <prom/registry.hpp>
#include <prom/summary.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::Summary;
using prom::test::RecordingAdapter;

namespace {

class SummaryTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(SummaryTest, ObserveRecordsSamples) {
    auto summary = registry->summary({.name = "sizes", .help = "h"});
    summary.observe(100.0);
    summary.observe(250.0);
    EXPECT_EQ(backend->family("sizes")->count("observe"), 2U);
    EXPECT_DOUBLE_EQ(backend->family("sizes")->total("observe"), 350.0);
}

TEST_F(SummaryTest, DefaultQuantiles) {
    EXPECT_EQ(prom::default_summary_quantiles.size(), 3U);
    EXPECT_DOUBLE_EQ(prom::default_summary_quantiles.front(), 0.5);
}
