/// @file
/// @brief End-to-end tests for the prometheus-cpp backend: drive prom metrics,
///        then assert on the text the prometheus-cpp registry would scrape.

#include <prom/prometheus_cpp/adapter.hpp>
#include <prom/registry.hpp>

#include <gtest/gtest.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <memory>
#include <string>

using prom::Registry;
using prom::prometheus_cpp::PrometheusCppAdapter;

namespace {

std::string scrape(const PrometheusCppAdapter& adapter) {
    return ::prometheus::TextSerializer().Serialize(adapter.registry().Collect());
}

}  // namespace

TEST(PrometheusCpp, BackendName) {
    const PrometheusCppAdapter adapter;
    EXPECT_EQ(adapter.backend_name(), "prometheus-cpp");
}

TEST(PrometheusCpp, CounterIncrements) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    auto counter = registry->counter({.name = "jobs_total", .help = "processed jobs"});
    counter.inc();
    counter.inc(4.0);

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("jobs_total"), std::string::npos);
    EXPECT_NE(text.find("jobs_total 5"), std::string::npos);
}

TEST(PrometheusCpp, GaugeSetAndLabels) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    auto gauge = registry->gauge(
        {.name = "queue_depth", .help = "depth", .labels = prom::Labels{{"queue", "ingest"}}});
    gauge.set(7.0);
    gauge.labels(prom::Labels{{"shard", "1"}}).set(3.0);

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("queue_depth{"), std::string::npos);
    EXPECT_NE(text.find("queue=\"ingest\""), std::string::npos);
    EXPECT_NE(text.find("shard=\"1\""), std::string::npos);
}

TEST(PrometheusCpp, HistogramObserves) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    auto hist =
        registry->histogram({.name = "req_seconds", .help = "latency", .buckets = {0.1, 0.5, 1.0}});
    hist.observe(0.05);
    hist.observe(0.3);
    hist.observe(2.0);

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("req_seconds_bucket"), std::string::npos);
    EXPECT_NE(text.find("req_seconds_count 3"), std::string::npos);
}

TEST(PrometheusCpp, SummaryObserves) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    auto summary = registry->summary({.name = "payload_bytes", .help = "sizes"});
    summary.observe(100.0);
    summary.observe(200.0);

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("payload_bytes_count 2"), std::string::npos);
    EXPECT_NE(text.find("payload_bytes_sum 300"), std::string::npos);
}

TEST(PrometheusCpp, StateSetExposesOneSeriesPerState) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    const auto ss = registry->stateset(
        {.name = "service_state", .help = "lifecycle", .states = {"running", "stopped"}});
    ss.set("running", true);
    ss.set("stopped", false);

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("service_state{service_state=\"running\"} 1"), std::string::npos);
    EXPECT_NE(text.find("service_state{service_state=\"stopped\"} 0"), std::string::npos);
}

TEST(PrometheusCpp, InfoExposesLabelsAtValueOne) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    const auto info = registry->info({.name = "build_info", .help = "build metadata"});
    info.set({{"version", "0.1.0"}});

    const std::string text = scrape(*adapter);
    EXPECT_NE(text.find("build_info{version=\"0.1.0\"} 1"), std::string::npos);
}
