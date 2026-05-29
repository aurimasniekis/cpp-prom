/// @file
/// @brief Per-library Scope: name prefixing, default-label merging, global
///        registration/retrieval, and — crucially — that the scope config is
///        live, so reconfiguring it reconfigures already-created metrics.

#include <prom/global.hpp>
#include <prom/scope.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::test::RecordingAdapter;

namespace {

class ScopeTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();

    void SetUp() override {
        prom::Registry::global()->set_adapter(backend);
    }
    void TearDown() override {
        prom::Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(ScopeTest, PrefixesMetricNames) {
    const auto foo = prom::scope("foo", {.prefix = "foo_"});
    auto c = foo->counter({.name = "requests_total", .help = "h"});
    c.inc(2.0);

    ASSERT_NE(backend->family("foo_requests_total"), nullptr);
    EXPECT_DOUBLE_EQ(backend->family("foo_requests_total")->total("inc"), 2.0);
}

TEST_F(ScopeTest, MergesDefaultLabelsWithMetricLabelsWinning) {
    const auto lib = prom::scope(
        "lib", {.prefix = "lib_", .const_labels = prom::Labels{{"lib", "foo"}, {"env", "prod"}}});
    auto gauge = lib->gauge({.name = "up", .help = "h", .labels = prom::Labels{{"env", "dev"}}});
    gauge.set(1.0);

    const auto family = backend->family("lib_up");
    ASSERT_NE(family, nullptr);
    const auto labels = family->const_labels;
    ASSERT_EQ(labels.size(), 2U);
    EXPECT_EQ(labels.view()[0].name, "env");
    EXPECT_EQ(labels.view()[0].value, "dev");  // metric's own label wins
    EXPECT_EQ(labels.view()[1].name, "lib");
    EXPECT_EQ(labels.view()[1].value, "foo");
}

TEST_F(ScopeTest, DefaultPrefixIsNamePlusUnderscore) {
    const auto svc = prom::scope("svc");  // no config -> prefix "svc_"
    EXPECT_EQ(svc->prefix(), "svc_");
    const auto c = svc->counter({.name = "events_total", .help = "h"});
    c.inc();
    EXPECT_NE(backend->family("svc_events_total"), nullptr);
}

TEST_F(ScopeTest, GlobalRegistrationReturnsSameInstance) {
    const auto first = prom::scope("dup", {.prefix = "dup_"});
    const auto second = prom::scope("dup");  // existing -> same instance, config ignored
    EXPECT_EQ(first, second);
    EXPECT_EQ(prom::Registry::scope("dup"), first);
    EXPECT_EQ(prom::find_scope("dup"), first);
}

TEST_F(ScopeTest, FindScopeReturnsNullForUnknown) {
    EXPECT_EQ(prom::find_scope("never_created"), nullptr);
}

// The headline behavior: changing the scope config at runtime reconfigures a
// metric that already exists. Subsequent samples flow to the newly-derived
// series; the old one stops updating.
TEST_F(ScopeTest, ReconfiguringPrefixAffectsExistingMetric) {
    const auto scope = prom::scope("svc", {.prefix = "svc_"});
    auto counter = scope->counter({.name = "requests_total", .help = "h"});

    counter.inc(2.0);  // -> svc_requests_total
    EXPECT_DOUBLE_EQ(backend->family("svc_requests_total")->total("inc"), 2.0);

    scope->set_prefix("srv_");
    counter.inc(3.0);  // same metric object, now -> srv_requests_total

    EXPECT_DOUBLE_EQ(backend->family("srv_requests_total")->total("inc"), 3.0);
    // The original series is untouched by the post-reconfigure sample.
    EXPECT_DOUBLE_EQ(backend->family("svc_requests_total")->total("inc"), 2.0);
}

TEST_F(ScopeTest, AddingDefaultLabelReconfiguresExistingMetric) {
    const auto scope = prom::scope("svc", {.prefix = "svc_"});
    auto gauge = scope->gauge({.name = "up", .help = "h"});
    gauge.set(1.0);

    scope->add_const_label("region", "eu");
    gauge.set(1.0);  // re-registers with the new constant label

    // The version bump forced a re-registration (two families share the name,
    // but the live one now carries the region label).
    EXPECT_GE(backend->register_count(), 2U);
}
