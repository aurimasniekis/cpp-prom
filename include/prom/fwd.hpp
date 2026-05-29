#pragma once

/// @file
/// @brief Forward declarations and `shared_ptr` aliases for the prom public API.
///
/// Pull this in when you only need to name a prom type (e.g. in a header that
/// stores a `prom::AdapterPtr`) without paying for the full definitions. The
/// umbrella `<prom/prom.hpp>` includes everything.

#include <memory>

namespace prom {

class Adapter;
class AdapterSource;
class MetricState;
class NullAdapter;
class Registry;
struct MetricInfo;

template <class Derived>
class MetricBase;

class Counter;
class Gauge;
class Histogram;
class Summary;
class Untyped;
class Info;
class StateSet;

/// Opaque, backend-owned handle to a registered metric family or a labeled
/// child series. Always non-null once handed back by an `Adapter`.
using MetricHandle = std::shared_ptr<MetricState>;

/// Shared ownership of an `Adapter`. The process-wide default and every
/// `Registry` hold one of these.
using AdapterPtr = std::shared_ptr<Adapter>;

}  // namespace prom
