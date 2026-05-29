/// @file
/// @brief The process-wide Registry::global() and the free prom::counter(...)
///        helpers: no registry needs to be passed around, and the global
///        registry follows whatever adapter is installed on the global cell.

#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class GlobalRegistry : public ::testing::Test {
protected:
    void TearDown() override {
        Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(GlobalRegistry, GlobalReturnsTheSameInstance) {
    EXPECT_EQ(prom::Registry::global(), prom::Registry::global());
}

TEST_F(GlobalRegistry, FreeHelperRecordsThroughInstalledAdapter) {
    const auto backend = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(backend);

    auto counter = prom::counter({.name = "free_total", .help = "no registry passed around"});
    counter.inc(5.0);

    ASSERT_NE(backend->family("free_total"), nullptr);
    EXPECT_DOUBLE_EQ(backend->family("free_total")->total("inc"), 5.0);
}

TEST_F(GlobalRegistry, NewMetricsLandOnTheCurrentAdapter) {
    const auto first = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(first);
    auto a = prom::gauge({.name = "g_one", .help = "h"});
    a.set(1.0);

    // Swap the adapter; a metric created now must land on the new one.
    const auto second = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(second);
    auto b = prom::gauge({.name = "g_two", .help = "h"});
    b.set(2.0);

    EXPECT_NE(first->family("g_one"), nullptr);
    EXPECT_EQ(first->family("g_two"), nullptr);
    EXPECT_NE(second->family("g_two"), nullptr);
    EXPECT_EQ(second->family("g_one"), nullptr);
}

TEST_F(GlobalRegistry, TryHelperReportsValidationErrors) {
    auto result = prom::try_counter({.name = "9invalid", .help = "h"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, prom::ErrorCode::InvalidMetricName);
}
