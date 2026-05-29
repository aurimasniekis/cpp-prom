/// @file
/// @brief StateSet behavior: toggling individual boolean states.

#include <prom/registry.hpp>
#include <prom/stateset.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::StateSet;
using prom::test::RecordingAdapter;

namespace {

class StateSetTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingAdapter> backend = std::make_shared<RecordingAdapter>();
    std::shared_ptr<Registry> registry = Registry::create(backend);
};

}  // namespace

TEST_F(StateSetTest, SetStates) {
    const auto ss = registry->stateset(
        {.name = "phase", .help = "h", .states = {"starting", "running", "stopped"}});
    ss.set("running", true);
    ss.set("starting", false);

    const auto family = backend->family("phase");
    ASSERT_NE(family, nullptr);
    ASSERT_TRUE(family->states.contains("running"));
    EXPECT_TRUE(family->states.at("running"));
    EXPECT_FALSE(family->states.at("starting"));
}
