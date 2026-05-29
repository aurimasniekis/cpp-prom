/// @file
/// @brief Process-wide decoration: a prefix / labels installed on the global
///        registry reaches standalone, global-registry, and scoped metrics, and
///        a scope's own decoration composes on top of the global one.

#include <prom/registry.hpp>
#include <prom/scope.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>
#include <string>
#include <string_view>

using prom::Counter;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

[[nodiscard]] std::string label_value(const prom::Labels& labels, std::string_view name) {
    for (const auto& [n, v] : labels.view()) {
        if (n == name) {
            return v;
        }
    }
    return {};
}

// The global decoration is process-wide, so every test here must leave it empty
// (and the adapter reset) or it would leak into unrelated tests.
class GlobalDecoration : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();

    void SetUp() override {
        Registry::global()->set_adapter(backend);
    }
    void TearDown() override {
        Registry::global()->configure(prom::RegistryConfig{});
        Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(GlobalDecoration, PrefixAppliesToGlobalRegistryMetrics) {
    Registry::global()->set_prefix("reg_");
    auto c = prom::counter({.name = "requests_total", .help = "h"});
    c.inc();

    EXPECT_EQ(c.name(), "reg_requests_total");
    EXPECT_NE(backend->family("reg_requests_total"), nullptr);
}

TEST_F(GlobalDecoration, PrefixAppliesToStandaloneMetrics) {
    Registry::global()->set_prefix("reg_");
    Counter c{"standalone_total", "h"};  // direct ctor, bypasses the registry
    c.inc();

    EXPECT_EQ(c.name(), "reg_standalone_total");
    EXPECT_NE(backend->family("reg_standalone_total"), nullptr);
}

TEST_F(GlobalDecoration, SettingPrefixReRegistersAlreadyCreatedMetrics) {
    Counter c{"late_total", "h"};
    c.inc();  // registers as late_total under the (empty) global decoration
    ASSERT_NE(backend->family("late_total"), nullptr);

    Registry::global()->set_prefix("reg_");
    c.inc();  // next use notices the global version bump and re-registers

    EXPECT_NE(backend->family("reg_late_total"), nullptr);
}

TEST_F(GlobalDecoration, GlobalConstLabelsReachMetrics) {
    Registry::global()->add_const_label("region", "eu");
    auto c = prom::counter({.name = "c_total", .help = "h"});
    c.inc();

    const auto family = backend->family("c_total");
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(label_value(family->const_labels, "region"), "eu");
}

TEST_F(GlobalDecoration, ScopePrefixComposesOnTopOfGlobalPrefix) {
    Registry::global()->set_prefix("reg_");
    auto scope = prom::scope("gdcompose", {.prefix = "foo_"});
    auto c = scope->counter({.name = "meter", .help = "h"});
    c.inc();

    // global is applied outermost: reg_ + foo_ + meter
    EXPECT_EQ(c.name(), "reg_foo_meter");
    EXPECT_NE(backend->family("reg_foo_meter"), nullptr);

    // enumeration reports the same composed name
    EXPECT_EQ(scope->metrics()[0].name, "reg_foo_meter");
}

TEST_F(GlobalDecoration, LabelPrecedenceIsOwnThenScopeThenGlobal) {
    Registry::global()->set_const_labels(prom::Labels{{"tier", "global"}, {"region", "eu"}});
    auto scope = prom::scope("gdlabels", {.const_labels = prom::Labels{{"tier", "scope"}}});
    auto c =
        scope->counter({.name = "m_total", .help = "h", .labels = prom::Labels{{"region", "us"}}});
    c.inc();

    const auto family = backend->family("m_total");
    ASSERT_NE(family, nullptr);
    EXPECT_EQ(label_value(family->const_labels, "tier"), "scope");  // scope beats global
    EXPECT_EQ(label_value(family->const_labels, "region"), "us");   // own beats global
}

TEST_F(GlobalDecoration, ChangingGlobalReRegistersScopedMetricsLive) {
    auto scope = prom::scope("gdlive", {.prefix = "foo_"});
    auto c = scope->counter({.name = "meter", .help = "h"});
    c.inc();
    ASSERT_NE(backend->family("foo_meter"), nullptr);  // no global prefix yet

    Registry::global()->set_prefix("reg_");
    c.inc();

    EXPECT_EQ(c.name(), "reg_foo_meter");
    EXPECT_NE(backend->family("reg_foo_meter"), nullptr);
}
