/// @file
/// @brief `CompositeAdapter` must fan every registration, resolution, and
///        mutation out to each wrapped backend, while staying a safe non-null,
///        non-throwing `Adapter`.

#include <prom/composite_adapter.hpp>
#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>
#include <vector>

using prom::CompositeAdapter;
using prom::CompositeState;
using prom::Counter;
using prom::Labels;
using prom::MetricMeta;
using prom::MetricType;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class CompositeTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> a = std::make_shared<RecordingAdapter>();
    std::shared_ptr<RecordingAdapter> b = std::make_shared<RecordingAdapter>();
    std::shared_ptr<CompositeAdapter> composite =
        std::make_shared<CompositeAdapter>(std::vector<prom::AdapterPtr>{a, b});
};

}  // namespace

TEST_F(CompositeTest, BackendName) {
    EXPECT_EQ(composite->backend_name(), "composite");
}

TEST_F(CompositeTest, DropsNullAdapters) {
    const CompositeAdapter c{a, nullptr, b};
    EXPECT_EQ(c.adapters().size(), 2U);
}

TEST_F(CompositeTest, RegisterFansOutToEveryChild) {
    const MetricMeta meta{.type = MetricType::Counter, .name = "x_total", .help = "h"};
    const auto handle = composite->register_metric(meta);

    ASSERT_NE(handle, nullptr);
    EXPECT_NE(a->family("x_total"), nullptr);
    EXPECT_NE(b->family("x_total"), nullptr);

    auto* state = dynamic_cast<CompositeState*>(handle.get());
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->handles.size(), 2U);
}

TEST_F(CompositeTest, MutationsReachEveryChild) {
    const MetricMeta meta{.type = MetricType::Counter, .name = "x_total", .help = "h"};
    const auto handle = composite->register_metric(meta);

    composite->inc(handle, 3.0);
    composite->inc(handle, 1.5);

    EXPECT_DOUBLE_EQ(a->family("x_total")->total("inc"), 4.5);
    EXPECT_DOUBLE_EQ(b->family("x_total")->total("inc"), 4.5);
}

TEST_F(CompositeTest, ResolveFansChildrenOutPerBackend) {
    const MetricMeta meta{.type = MetricType::Counter, .name = "x_total", .help = "h"};
    const auto family = composite->register_metric(meta);

    const auto child = composite->resolve(family, Labels{{"code", "200"}});
    ASSERT_NE(child, nullptr);
    EXPECT_NE(dynamic_cast<CompositeState*>(child.get()), nullptr);

    composite->inc(child, 2.0);
    // Each child backend cached its own labeled series; the family stays at 0.
    EXPECT_DOUBLE_EQ(a->family("x_total")->total("inc"), 0.0);
    EXPECT_DOUBLE_EQ(b->family("x_total")->total("inc"), 0.0);
}

TEST_F(CompositeTest, MutatingForeignHandleIsANoOp) {
    const auto foreign = std::make_shared<prom::MetricState>();
    EXPECT_NO_THROW(composite->inc(foreign, 1.0));
    EXPECT_NO_THROW(composite->observe(foreign, 1.0));
}

TEST_F(CompositeTest, EmptyCompositeIsASafeSink) {
    CompositeAdapter empty{std::vector<prom::AdapterPtr>{}};
    const MetricMeta meta{.type = MetricType::Gauge, .name = "g", .help = "h"};
    const auto handle = empty.register_metric(meta);
    ASSERT_NE(handle, nullptr);
    EXPECT_NO_THROW(empty.set(handle, 5.0));
}

TEST_F(CompositeTest, DrivesEveryBackendThroughTheRegistry) {
    const auto registry = Registry::create(composite);
    auto counter = registry->counter({.name = "reqs_total", .help = "h"});
    counter.inc();
    counter.inc(4.0);

    EXPECT_DOUBLE_EQ(a->family("reqs_total")->total("inc"), 5.0);
    EXPECT_DOUBLE_EQ(b->family("reqs_total")->total("inc"), 5.0);
}
