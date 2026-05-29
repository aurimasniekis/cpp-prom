/// @file
/// @brief The NullAdapter must be a safe, non-throwing no-op that always hands
///        back valid handles.

#include <prom/adapter.hpp>
#include <prom/labels.hpp>
#include <prom/null_adapter.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>

using prom::Label;
using prom::Labels;
using prom::MetricMeta;
using prom::MetricType;
using prom::NullAdapter;

TEST(NullAdapter, BackendName) {
    const NullAdapter adapter;
    EXPECT_EQ(adapter.backend_name(), "null");
}

TEST(NullAdapter, RegisterReturnsNonNullHandle) {
    NullAdapter adapter;
    const MetricMeta meta{.type = MetricType::Counter, .name = "x_total", .help = "h"};
    const auto handle = adapter.register_metric(meta);
    ASSERT_NE(handle, nullptr);
}

TEST(NullAdapter, ResolveReturnsNonNullHandle) {
    NullAdapter adapter;
    const MetricMeta meta{.type = MetricType::Gauge, .name = "g", .help = "h"};
    const auto family = adapter.register_metric(meta);
    const Labels dynamic{{"a", "b"}};
    EXPECT_NE(adapter.resolve(family, dynamic), nullptr);
}

TEST(NullAdapter, MutationsNeverThrow) {
    NullAdapter adapter;
    const MetricMeta meta{.type = MetricType::Histogram, .name = "h", .help = "help"};
    const auto handle = adapter.register_metric(meta);

    EXPECT_NO_THROW(adapter.inc(handle, 1.0));
    EXPECT_NO_THROW(adapter.dec(handle, 2.0));
    EXPECT_NO_THROW(adapter.set(handle, 3.0));
    EXPECT_NO_THROW(adapter.observe(handle, 4.0));

    const std::array<Label, 1> labels = {Label{"k", "v"}};
    EXPECT_NO_THROW(adapter.set_info(handle, std::span<const Label>(labels)));
    EXPECT_NO_THROW(adapter.set_state(handle, "running", true));
    EXPECT_NO_THROW(adapter.set_unit(handle, prom::Unit{"seconds", "time", "s", true}));
}
