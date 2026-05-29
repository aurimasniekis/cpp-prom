/// @file
/// @brief Registry: spec -> registration, and validation through the try_* API.

#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::ErrorCode;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class RegistryTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(RegistryTest, RegistersEachMetricType) {
    auto counter = registry->counter({.name = "requests_total", .help = "h"});
    auto gauge = registry->gauge({.name = "in_flight", .help = "h"});
    auto hist = registry->histogram({.name = "latency", .help = "h"});
    auto summ = registry->summary({.name = "sizes", .help = "h"});
    auto unt = registry->untyped({.name = "raw", .help = "h"});
    auto inf = registry->info({.name = "build", .help = "h"});
    auto ss = registry->stateset({.name = "state", .help = "h", .states = {"on", "off"}});

    EXPECT_EQ(backend->register_count(), 7U);
    EXPECT_NE(backend->family("requests_total"), nullptr);
    EXPECT_EQ(backend->family("latency")->type, prom::MetricType::Histogram);
}

TEST_F(RegistryTest, ConstLabelsReachTheBackend) {
    auto counter = registry->counter(
        {.name = "c_total", .help = "h", .labels = prom::Labels{{"service", "api"}}});
    const auto family = backend->family("c_total");
    ASSERT_NE(family, nullptr);
    ASSERT_EQ(family->const_labels.size(), 1U);
    EXPECT_EQ(family->const_labels.view()[0].value, "api");
}

TEST_F(RegistryTest, TryCounterRejectsInvalidName) {
    auto result = registry->try_counter({.name = "9bad", .help = "h"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidMetricName);
}

TEST_F(RegistryTest, TryCounterRejectsInvalidLabel) {
    auto result = registry->try_counter(
        {.name = "ok_total", .help = "h", .labels = prom::Labels{{"__reserved", "x"}}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidLabelName);
}

TEST_F(RegistryTest, ThrowingFactoryRaisesOnInvalidName) {
    EXPECT_THROW((void)registry->counter({.name = "has space", .help = "h"}), prom::Exception);
}

TEST_F(RegistryTest, HistogramRejectsUnsortedBuckets) {
    auto result = registry->try_histogram({.name = "h", .help = "h", .buckets = {1.0, 0.5}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidBuckets);
}

TEST_F(RegistryTest, SummaryRejectsOutOfRangeQuantile) {
    auto result = registry->try_summary({.name = "s", .help = "h", .quantiles = {1.5}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidQuantiles);
}

TEST_F(RegistryTest, StateSetRejectsEmptyStates) {
    auto result = registry->try_stateset({.name = "s", .help = "h"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::EmptyStateSet);
}

// --- registry-level decoration (prefix / labels / display) ------------------

namespace {

[[nodiscard]] std::string label_value(const prom::Labels& labels, const std::string_view name) {
    for (const auto& [n, v] : labels.view()) {
        if (n == name) {
            return v;
        }
    }
    return {};
}

}  // namespace

TEST(RegistryDecoration, PrefixAndConstLabelsApplyToEveryMetric) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(
        backend, {.prefix = "svc_", .const_labels = prom::Labels{{"region", "eu"}}});

    const auto counter = registry->counter(
        {.name = "requests_total", .help = "h", .labels = prom::Labels{{"route", "/"}}});

    EXPECT_EQ(counter.name(), "svc_requests_total");
    const auto family = backend->family("svc_requests_total");
    ASSERT_NE(family, nullptr);
    // Registry const label merged with the metric's own; own label preserved.
    ASSERT_EQ(family->const_labels.size(), 2U);
    EXPECT_EQ(label_value(family->const_labels, "region"), "eu");
    EXPECT_EQ(label_value(family->const_labels, "route"), "/");
}

TEST(RegistryDecoration, MetricOwnLabelWinsOnCollision) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry =
        Registry::create(backend, {.const_labels = prom::Labels{{"region", "eu"}}});

    auto counter = registry->counter(
        {.name = "c_total", .help = "h", .labels = prom::Labels{{"region", "us"}}});

    const auto family = backend->family("c_total");
    ASSERT_NE(family, nullptr);
    ASSERT_EQ(family->const_labels.size(), 1U);
    EXPECT_EQ(label_value(family->const_labels, "region"), "us");
}

TEST(RegistryDecoration, ListedMetricsReportEffectiveNameAndScopedFlag) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(backend, {.prefix = "svc_"});
    auto counter = registry->counter({.name = "reqs_total", .help = "h"});

    const auto infos = registry->metrics();
    ASSERT_EQ(infos.size(), 1U);
    EXPECT_EQ(infos[0].name, "svc_reqs_total");
    EXPECT_TRUE(infos[0].scoped);
}

TEST(RegistryDecoration, ReconfiguringReRegistersExistingMetricLive) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(backend, {.prefix = "svc_"});
    const auto counter = registry->counter({.name = "reqs_total", .help = "h"});
    counter.inc();
    ASSERT_NE(backend->family("svc_reqs_total"), nullptr);

    registry->set_prefix("api_");
    counter.inc();  // next use notices the new decoration version and re-registers

    EXPECT_NE(backend->family("api_reqs_total"), nullptr);
    EXPECT_EQ(registry->metrics()[0].name, "api_reqs_total");
}

TEST(RegistryDecoration, UndecoratedRegistryLeavesNamesAndScopedFlagUntouched) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(backend);
    const auto counter = registry->counter({.name = "plain_total", .help = "h"});

    EXPECT_EQ(counter.name(), "plain_total");
    EXPECT_FALSE(registry->metrics()[0].scoped);
}

TEST(RegistryDecoration, ValidationAppliesToTheBaseName) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(backend, {.prefix = "svc_"});

    auto result = registry->try_counter({.name = "9bad", .help = "h"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidMetricName);
}

TEST(RegistryDecoration, SetterReRegistersMetricsCreatedBeforeDecorationWasSet) {
    const auto backend = std::make_shared<RecordingAdapter>();
    const auto registry = Registry::create(backend);  // starts undecorated
    auto counter = registry->counter({.name = "plain_total", .help = "h"});
    counter.inc();
    ASSERT_NE(backend->family("plain_total"), nullptr);

    registry->set_prefix("svc_");  // affects the already-created metric
    counter.inc();

    EXPECT_EQ(counter.name(), "svc_plain_total");
    EXPECT_NE(backend->family("svc_plain_total"), nullptr);
    EXPECT_TRUE(registry->metrics()[0].scoped);
}
