#pragma once

/// @file
/// @brief Umbrella header — pulls in the entire prom **core** API.
///
/// This intentionally does *not* include any backend adapter (e.g. the
/// prometheus-cpp module). Backends ship and are included with their own
/// module header (`<prom/prometheus_cpp/adapter.hpp>`), mirroring how conduit
/// keeps its transports out of the core umbrella. Install a backend at startup
/// with `prom::Registry::global()->set_adapter(...)`.

#include <prom/adapter.hpp>
#include <prom/composite_adapter.hpp>
#include <prom/counter.hpp>
#include <prom/dimval.hpp>
#include <prom/error.hpp>
#include <prom/fwd.hpp>
#include <prom/gauge.hpp>
#include <prom/global.hpp>
#include <prom/histogram.hpp>
#include <prom/info.hpp>
#include <prom/labels.hpp>
#include <prom/metric_base.hpp>
#include <prom/null_adapter.hpp>
#include <prom/registry.hpp>
#include <prom/scope.hpp>
#include <prom/stateset.hpp>
#include <prom/summary.hpp>
#include <prom/unit.hpp>
#include <prom/untyped.hpp>
#include <prom/version.hpp>
