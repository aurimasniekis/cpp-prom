/// @file
/// @brief Info behavior: set label payload via initializer_list and Labels.

#include <prom/info.hpp>
#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Info;
using prom::Labels;
using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

class InfoTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(InfoTest, SetFromInitializerList) {
    const auto info = registry->info({.name = "build", .help = "h"});
    info.set({{"version", "1.2.3"}, {"commit", "abc"}});
    const auto family = backend->family("build");
    ASSERT_NE(family, nullptr);
    ASSERT_EQ(family->info_labels.size(), 2U);
}

TEST_F(InfoTest, SetFromLabels) {
    const auto info = registry->info({.name = "build", .help = "h"});
    info.set(Labels{{"version", "9.9"}});
    const auto family = backend->family("build");
    ASSERT_NE(family, nullptr);
    ASSERT_EQ(family->info_labels.size(), 1U);
    EXPECT_EQ(family->info_labels[0].value, "9.9");
}
