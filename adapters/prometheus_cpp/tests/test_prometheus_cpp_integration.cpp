/// @file
/// @brief HTTP-level integration test for the prometheus-cpp backend.
///
/// Records metrics through prom's client-independent API, serves them from a
/// real `prometheus::Exposer` (the Civetweb-backed pull endpoint a host
/// application uses), then scrapes `/metrics` over an actual TCP/HTTP round trip
/// and asserts the exposition text came back intact. Unlike the in-process
/// serializer tests, this exercises the full pull path end to end over a socket.
///
/// POSIX sockets only (the CI matrix is Linux + macOS); guarded accordingly so
/// the file is simply skipped elsewhere.

#include <prom/prometheus_cpp/adapter.hpp>
#include <prom/registry.hpp>

#include <gtest/gtest.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#if defined(__unix__) || defined(__APPLE__)

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using prom::Registry;
using prom::prometheus_cpp::PrometheusCppAdapter;

namespace {

/// Minimal blocking HTTP/1.1 GET over a TCP socket — enough to scrape a
/// localhost `/metrics` endpoint without taking on an HTTP-client dependency.
/// Returns the response body, or `std::nullopt` if the round trip fails.
std::optional<std::string>
http_get(const std::string& host, const int port, const std::string& path) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return std::nullopt;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    const std::string request =
        "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    if (::send(fd, request.data(), request.size(), 0) < 0) {
        ::close(fd);
        return std::nullopt;
    }
    std::string response;
    std::array<char, 4096> buf{};
    for (;;) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            break;
        }
        response.append(buf.data(), static_cast<std::size_t>(n));
    }
    ::close(fd);
    const auto split = response.find("\r\n\r\n");
    if (split == std::string::npos) {
        return std::nullopt;  // No header/body boundary — malformed.
    }
    return response.substr(split + 4);
}

}  // namespace

TEST(PrometheusCppIntegration, ScrapesOverRealHttp) {
    const auto adapter = std::make_shared<PrometheusCppAdapter>();
    const auto registry = Registry::create(adapter);

    // A deterministic spread across metric types and a labeled child.
    auto requests =
        registry->counter({.name = "integration_requests_total", .help = "requests handled"});
    requests.inc();
    requests.inc(2.0);  // total 3

    auto in_flight = registry->gauge({.name = "integration_in_flight", .help = "in-flight"});
    in_flight.set(5.0);

    const auto responses =
        registry->counter({.name = "integration_responses_total", .help = "responses by code"});
    responses.labels(prom::Labels{{"code", "200"}}).inc(4.0);

    // Bring up a real HTTP exposer on the first free port from a small candidate
    // set (the Exposer constructor throws if the port is already taken).
    std::unique_ptr<prometheus::Exposer> exposer;
    int port = 0;
    for (const int candidate : {19191, 19192, 19193, 19194, 19195}) {
        try {
            exposer =
                std::make_unique<prometheus::Exposer>("127.0.0.1:" + std::to_string(candidate));
            port = candidate;
            break;
        } catch (const std::exception&) {
            // Port busy — try the next candidate.
        }
    }
    ASSERT_NE(exposer, nullptr) << "could not bind an Exposer on any candidate port";
    exposer->RegisterCollectable(adapter->registry_ptr());

    // Scrape /metrics over a real socket, retrying briefly in case the server
    // thread is still warming up.
    std::optional<std::string> body;
    for (int attempt = 0; attempt < 50; ++attempt) {
        body = http_get("127.0.0.1", port, "/metrics");
        if (body && body->find("integration_requests_total") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(body.has_value()) << "no HTTP response from the exposer";
    EXPECT_NE(body->find("integration_requests_total 3"), std::string::npos) << *body;
    EXPECT_NE(body->find("integration_in_flight 5"), std::string::npos) << *body;
    EXPECT_NE(body->find("integration_responses_total{code=\"200\"} 4"), std::string::npos)
        << *body;
}

#endif  // POSIX
