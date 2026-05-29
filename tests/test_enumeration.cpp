/// @file
/// @brief Enumeration APIs: Registry::metrics(), Scope::metrics(),
///        prom::scopes() and prom::scope_names(), all returning read-only
///        MetricInfo snapshots.

#include <prom/registry.hpp>
#include <prom/scope.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <algorithm>
#include <memory>
#include <string>

using prom::MetricInfo;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

const MetricInfo* find(const std::vector<MetricInfo>& infos, const std::string_view name) {
    for (const auto& info : infos) {
        if (info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

}  // namespace

TEST(Enumeration, RegistryListsItsMetrics) {
    auto registry = Registry::create(std::make_shared<RecordingAdapter>());

    auto counter =
        registry->counter({.name = "a_total", .help = "ha", .labels = prom::Labels{{"k", "v"}}});
    auto gauge = registry->gauge(
        {.name = "b_seconds", .help = "hb", .unit = prom::Unit{"seconds", "time", "s"}});

    const auto infos = registry->metrics();
    ASSERT_EQ(infos.size(), 2U);

    const MetricInfo* a = find(infos, "a_total");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->type, prom::MetricType::Counter);
    EXPECT_EQ(a->help, "ha");
    EXPECT_FALSE(a->scoped);
    ASSERT_EQ(a->const_labels.size(), 1U);
    EXPECT_EQ(a->const_labels.view()[0].value, "v");

    const MetricInfo* b = find(infos, "b_seconds");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->type, prom::MetricType::Gauge);
    EXPECT_EQ(b->unit.name, "seconds");
    EXPECT_EQ(b->unit.kind, "time");
}

TEST(Enumeration, RegistryPrunesExpiredMetrics) {
    const auto registry = Registry::create(std::make_shared<RecordingAdapter>());
    {
        auto temp = registry->counter({.name = "ephemeral_total", .help = "h"});
        EXPECT_EQ(registry->metrics().size(), 1U);
    }
    // The metric handle is gone; the weak entry is pruned on the next listing.
    EXPECT_TRUE(registry->metrics().empty());
}

TEST(Enumeration, ScopeListsEffectiveNamesAndLabels) {
    const auto scope =
        prom::scope("enum_scope", {.prefix = "es_", .const_labels = prom::Labels{{"lib", "enum"}}});
    auto counter = scope->counter({.name = "reqs_total", .help = "h"});

    const auto infos = scope->metrics();
    ASSERT_EQ(infos.size(), 1U);
    EXPECT_EQ(infos[0].name, "es_reqs_total");  // effective (prefixed) name
    EXPECT_TRUE(infos[0].scoped);
    ASSERT_EQ(infos[0].const_labels.size(), 1U);
    EXPECT_EQ(infos[0].const_labels.view()[0].name, "lib");

    // Reconfiguring the scope is reflected live in the next snapshot.
    scope->set_prefix("svc_");
    EXPECT_EQ(scope->metrics()[0].name, "svc_reqs_total");
}

TEST(Enumeration, ScopesAndScopeNamesAreListable) {
    auto one = prom::scope("alpha");
    auto two = prom::scope("beta");

    const auto names = prom::scope_names();
    EXPECT_NE(std::ranges::find(names, "alpha"), names.end());
    EXPECT_NE(std::ranges::find(names, "beta"), names.end());

    const auto all = prom::scopes();
    EXPECT_EQ(all.size(), names.size());
    EXPECT_GE(all.size(), 2U);
}
