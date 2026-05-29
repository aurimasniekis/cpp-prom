/// @file
/// @brief Global registry adapter install/swap semantics via the adapter cell.

#include <prom/registry.hpp>

#include <gtest/gtest.h>

#include "recording_adapter.hpp"

#include <memory>

using prom::Registry;
using prom::test::RecordingAdapter;

namespace {

// Restore the global adapter after each test so ordering can't leak state.
class GlobalAdapter : public ::testing::Test {
protected:
    void TearDown() override {
        Registry::global()->set_adapter(nullptr);
    }
};

}  // namespace

TEST_F(GlobalAdapter, DefaultIsNeverNull) {
    EXPECT_NE(Registry::global()->adapter(), nullptr);
    EXPECT_EQ(Registry::global()->adapter()->backend_name(), "null");
}

TEST_F(GlobalAdapter, SetAdapterInstallsBackend) {
    const auto backend = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(backend);
    EXPECT_EQ(Registry::global()->adapter(), backend);
    EXPECT_EQ(Registry::global()->adapter()->backend_name(), "recording");
}

TEST_F(GlobalAdapter, NullptrResetsToNullAdapter) {
    Registry::global()->set_adapter(std::make_shared<RecordingAdapter>());
    EXPECT_EQ(Registry::global()->adapter()->backend_name(), "recording");

    Registry::global()->set_adapter(nullptr);
    EXPECT_EQ(Registry::global()->adapter()->backend_name(), "null");
}

TEST_F(GlobalAdapter, ReadReturnsAnIndependentCopy) {
    const auto installed = std::make_shared<RecordingAdapter>();
    Registry::global()->set_adapter(installed);

    const auto snapshot = Registry::global()->adapter();  // copy
    Registry::global()->set_adapter(nullptr);             // swap underneath

    // The in-flight snapshot is still valid and still points at our adapter.
    EXPECT_EQ(snapshot, installed);
    EXPECT_EQ(snapshot->backend_name(), "recording");
}
