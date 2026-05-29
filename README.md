# prom

[![CI](https://github.com/aurimasniekis/cpp-prom/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-prom/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-prom/actions/workflows/docs.yml/badge.svg)](https://github.com/aurimasniekis/cpp-prom/actions/workflows/docs.yml)

**A client-independent Prometheus / OpenMetrics metric abstraction for C++23.**

`prom` lets you declare and record Prometheus-style metrics (`Counter`, `Gauge`,
`Histogram`, `Summary`, `Untyped`, `Info`, `StateSet`) **without committing to a
concrete metrics client**. Your code records samples through `prom`'s small,
stable API. A separate *adapter* decides where those samples actually go. Until
an application installs a real backend, every metric resolves to a built-in
`NullAdapter` that turns each operation into a safe, logged no-op — so code that
records metrics runs unchanged whether or not a backend is present.

This is the headline use case: a **reusable library** can ship metric
instrumentation without forcing a Prometheus client dependency on everyone who
links it. The **host application** installs a backend once, at startup.

```cpp
#include <prom/prom.hpp>

// In a reusable library — no backend dependency, no registry to thread around:
auto requests = prom::counter({.name = "mylib_requests_total",
                               .help = "Requests handled by mylib"});
requests.inc();   // safe no-op until the application installs a backend
```

---

## Why use this library?

A C++ library that wants to expose operational metrics usually faces a choice:
hard-depend on a specific Prometheus client (and impose it on every downstream
user), or expose nothing. `prom` removes that choice.

- **Good for** instrumenting a library that should not dictate its consumers'
  metrics stack.
- **Good for** application code that wants a one-line `prom::counter(...)` API
  and the freedom to wire (or not wire) a backend later.
- **Avoids** leaking any backend type across the API boundary — the core never
  includes a prometheus-cpp header.
- **Useful when** you want metrics that are always safe to call, even before
  (or without) any exporter installed.
- **Useful when** you want to record dimensional quantities (seconds, bytes/s,
  hertz) and have the unit carried along automatically.
- **Not ideal for** a standalone application that already commits to one client
  and wants its native API directly — the indirection buys you nothing there.
- **Not ideal for** scrape/exposition itself: `prom` records samples; serving
  `/metrics` is the backend adapter's job (see [Enabling a real
  backend](#enabling-a-real-backend)).

## Quick example

The smallest useful program. It records a few samples; with no backend
installed they go to the `NullAdapter`.

```cpp
#include <prom/prom.hpp>

#include <iostream>

int main() {
    // The free helpers create metrics through the process-wide registry.
    // No Registry object to construct or pass around.
    auto requests  = prom::counter({.name = "demo_requests_total", .help = "demo"});
    auto in_flight = prom::gauge({.name = "demo_in_flight", .help = "demo"});

    requests.inc();          // +1
    requests.inc(10);        // +10
    in_flight.set(3);
    in_flight.dec();         // -1

    std::cout << "backend = "
              << prom::Registry::global()->adapter()->backend_name() << "\n";
    return 0;
}
```

Why it works:

- `prom::counter(...)` / `prom::gauge(...)` are free functions that delegate to
  the process-wide `Registry::global()`. You never have to create or pass a
  registry for the common case.
- Each metric is a cheap, copyable value. It binds to whatever adapter is
  installed on the global cell on first use — here, the default `NullAdapter`.
- Every mutation (`inc`, `set`, `dec`, ...) is `noexcept`. Nothing here can
  throw, and nothing is exported, because no real backend is installed.

Running it prints `backend = null`.

## Installation

`prom`'s **core is header-only** and requires C++23. Its three runtime
dependencies are fetched automatically by CMake when you don't already have them
installed:

- [commons](https://github.com/aurimasniekis/cpp-commons) — display metadata
  (`comms::DisplayInfo`).
- [logman](https://github.com/aurimasniekis/cpp-logman) — logging (over spdlog).
- [dimval](https://github.com/aurimasniekis/cpp-dimval) — dimensional value
  types (the core only matches them *structurally* — see [Dimensional
  values](#recording-dimensional-dimval-values)).

### CMake FetchContent (recommended)

```cmake
cmake_minimum_required(VERSION 3.25)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    prom
    URL      https://github.com/aurimasniekis/cpp-prom/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=0000000000000000000000000000000000000000000000000000000000000000
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(prom)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE prom::prom)
```

`prom`'s own dependencies are each declared with `FIND_PACKAGE_ARGS`, so an
installed copy is preferred over fetching when present.

### CMake find_package (installed copy)

```cmake
find_package(prom 0.1 REQUIRED)        # pulls commons / logman / dimval transitively
target_link_libraries(app PRIVATE prom::prom)
```

### Enabling the prometheus-cpp backend

The real backend is an **opt-in module** built from source, gated by a CMake
option. It is a compiled static library (not header-only):

```bash
cmake -S . -B build -DPROM_WITH_PROMETHEUS_CPP=ON
```

```cmake
target_link_libraries(app PRIVATE prom::prom prom::prometheus_cpp)
```

This pulls in [prometheus-cpp](https://github.com/jupp0r/prometheus-cpp) via
FetchContent and exposes the `prom::prometheus_cpp` target.

## Requirements

- **C++ standard:** C++23 (`cxx_std_23`; `CMAKE_CXX_EXTENSIONS OFF`).
- **CMake:** 3.25 or newer.
- **Dependencies (core):** commons, logman (+ spdlog), dimval — fetched
  automatically if not installed.
- **Optional:** prometheus-cpp, only when `-DPROM_WITH_PROMETHEUS_CPP=ON`.

## Core concepts

### Metrics are value types with lazy binding

Every metric (`Counter`, `Gauge`, ...) is a small, copyable object holding a
`shared_ptr` to shared per-series state. **Copies refer to the same series.**

```cpp
prom::Counter a{"shared_total", "h"};   // standalone, unbound
prom::Counter b = a;                    // a copy
a.inc(1);
b.inc(2);                               // same series — total is 3
```

A *standalone* metric (constructed directly from `name`/`help` or a spec) is
**unbound** until first use; on the first `inc`/`set`/`observe` it binds to
whatever adapter the process-wide cell currently holds and registers itself.

### `Registry` — the front door for registered metrics

A `Registry` owns the adapter its metrics record through and validates each spec
up front. You rarely need to construct one: `Registry::global()` is a
process-wide instance the free `prom::counter(...)` helpers delegate to.

```cpp
auto reg = prom::Registry::global();
auto c   = reg->counter({.name = "requests_total", .help = "..."});
```

`Registry` is non-copyable and `shared_ptr`-managed: obtain one with
`Registry::create(...)` or `Registry::global()` and call it through `->`. Reach
for an explicit registry when you want an independent adapter (e.g. in a test):

```cpp
auto backend = std::make_shared<my::Backend>();
auto pinned  = prom::Registry::create(backend);   // its own adapter cell
auto c       = pinned->counter({.name = "x_total", .help = "..."});
```

#### Registry-level prefix and labels

A registry can also **decorate** every metric it creates with a shared name
prefix, default constant labels, and default display — the same thing a
[`Scope`](#per-library-scopes-prefix--default-labels) does, but applied at the
registry level. Pass a `RegistryConfig` to `create`:

```cpp
auto reg = prom::Registry::create(backend, {.prefix       = "svc_",
                                            .const_labels = prom::Labels{{"region", "eu"}}});
auto c   = reg->counter({.name = "requests_total"});   // -> svc_requests_total{region="eu"}
```

The decoration is **live**: every metric the registry has created — *including
ones created before the change* — re-registers under the new name/labels on its
next use (a metric's own label still wins over a registry default on a name
collision). The setters work even on a registry created without a config (its
decoration simply starts empty):

```cpp
reg->set_prefix("api_");          // c re-registers as api_requests_total
reg->add_const_label("az", "eu-1");
```

A registry whose decoration is empty leaves metrics with their spec names
verbatim and reports `scoped == false` from `metrics()`.

#### A process-wide prefix on `Registry::global()`

`Registry::global()`'s decoration is the **process-wide** one, shared by
standalone metrics (`prom::counter(...)` and direct constructors) and used as the
chain parent of every [`Scope`](#per-library-scopes-prefix--default-labels). So a
prefix or label installed there reaches *every* metric in the process:

```cpp
prom::Registry::global()->set_prefix("svc_");      // every metric gains svc_*
prom::Registry::global()->add_const_label("region", "eu");
```

When a scope sits underneath, the two compose with the global prefix outermost —
a scope `foo_` under a global `reg_` yields `reg_foo_meter`. Label precedence is
**own → scope → global**.

### `Adapter` — the backend boundary

Everything funnels through one interface, `prom::Adapter`, in a
**register-then-mutate** model: a family is registered once
(`register_metric`), labeled children are obtained with `resolve`, and samples
are pushed with `inc`/`dec`/`set`/`observe`/`set_info`/`set_state`. No
backend-specific type ever crosses this line. Every adapter method is `noexcept`
and may be called concurrently.

The default `NullAdapter` records nothing: it logs registration at debug,
mutations at trace, and is stateless and fully thread-safe.

### Installing a backend on a cell

The adapter does **not** live on each metric; it lives on an `AdapterCell`
shared by the metrics that read from it. `Registry::global()` and all standalone
/ scoped metrics share the *process-wide* cell, so installing a backend there
reconfigures everything at once:

```cpp
prom::Registry::global()->set_adapter(std::make_shared<my::Backend>());
// passing nullptr resets the cell back to a fresh NullAdapter
```

## Common usage patterns

### Instrumenting a library with no backend dependency

This is what `prom` is for. The library declares metrics through the free
helpers and never mentions a backend:

```cpp
#include <prom/prom.hpp>

namespace mylib {

class Telemetry {
public:
    static Telemetry& instance() {
        static Telemetry t;
        return t;
    }

    prom::Counter   requests = prom::counter(
        {.name = "mylib_requests_total", .help = "Total requests handled by mylib"});
    prom::Histogram latency  = prom::histogram(
        {.name = "mylib_request_seconds", .help = "Request latency in seconds"});
};

void handle_request(double seconds) {
    Telemetry::instance().requests.inc();
    Telemetry::instance().latency.observe(seconds);
}

}  // namespace mylib
```

If the application never installs a backend, `mylib` still runs — the metrics
resolve to the `NullAdapter`. If it does, the same code starts exporting.

### Every metric type

```cpp
#include <prom/prom.hpp>

auto reg = prom::Registry::create();

auto counter = reg->counter({.name = "events_total", .help = "events"});
auto gauge   = reg->gauge({.name = "queue_depth", .help = "queue depth"});
auto hist    = reg->histogram({.name = "op_seconds", .help = "op latency",
                               .buckets = {0.1, 0.5, 1.0}});
auto summ    = reg->summary({.name = "payload_bytes", .help = "payload sizes"});
auto unt     = reg->untyped({.name = "external_value", .help = "raw"});
auto info    = reg->info({.name = "build_info", .help = "build metadata"});
auto state   = reg->stateset({.name = "service_state", .help = "lifecycle",
                              .states = {"starting", "running", "stopped"}});

counter.inc();                 // +1
counter.inc(7);                // +7
gauge.set(5);
gauge.dec();                   // -1
hist.observe(0.3);
summ.observe(2048);
unt.set(-1.5);                 // untyped: no sign constraints
info.set({{"version", "0.1.0"}, {"commit", "deadbeef"}});
state.set("running", true);    // one boolean member of the set
```

What each type does:

| Type        | Mutators                                       | Notes                                                                        |
|-------------|------------------------------------------------|------------------------------------------------------------------------------|
| `Counter`   | `inc()`, `inc(v)`                              | Monotonic. Negative / non-finite increments are dropped + logged.            |
| `Gauge`     | `set(v)`, `inc()`, `inc(v)`, `dec()`, `dec(v)` | Moves both directions.                                                       |
| `Histogram` | `observe(v)`                                   | Default buckets `{.005,.01,.025,.05,.1,.25,.5,1,2.5,5,10}` when unspecified. |
| `Summary`   | `observe(v)`                                   | Default quantiles `{0.5, 0.9, 0.99}` when unspecified.                       |
| `Untyped`   | `set(v)`                                       | No semantic constraints.                                                     |
| `Info`      | `set({{k, v}, ...})`, `set(Labels)`            | Static key/value metadata; rendered as `name_info{...} 1`.                   |
| `StateSet`  | `set(state, bool)`                             | A set of related boolean states; one series per state.                       |

### Recording raw arithmetic values

The simplest path. Any arithmetic type works — it routes through
`prom::normalize()` to a `double`:

```cpp
auto bytes = reg->counter({.name = "bytes_processed_total", .help = "bytes"});
auto temp  = reg->gauge({.name = "cpu_celsius", .help = "temperature"});

bytes.inc(512);     // int
bytes.inc(1024U);   // unsigned
temp.set(41.7);     // double
temp.set(39);       // int — fine
```

### Recording dimensional (dimval) values

A dimensional value carries its unit and dimensional *kind* alongside the
magnitude. `prom` reduces it to a `{value, Unit}` pair and **infers the metric's
unit from the first dimensional sample it sees**:

```cpp
#include <prom/prom.hpp>
#include <dimval/dimval.hpp>

auto latency    = reg->histogram({.name = "rpc_seconds",     .help = "RPC latency"});
auto throughput = reg->gauge    ({.name = "link_byte_rate",  .help = "throughput"});
auto tuned      = reg->gauge    ({.name = "radio_center_hz", .help = "tuned freq"});

latency.observe(dimval::SecondValue{0.0123});
throughput.set(dimval::ByteRateValue{1.25e6});
tuned.set(dimval::CenterFrequencyValue{100.3e6});
```

`prom` never includes a dimval header. The `DimensionalValue` concept matches
dimval's value types *structurally* (it just needs `value_t`,
`numeric_as_double()`, and a unit descriptor), so handing `prom` a dimensional
value does not drag dimval's concrete types into `prom`'s own translation units.

> **Pitfall — unit-kind mismatch.** Once a metric has latched a unit (declared
> or inferred), a later dimensional sample whose *kind* disagrees (e.g. a length
> value into a metric that latched `time`) is **dropped and logged, never
> thrown**. A raw (unitless) sample always passes. See [Edge cases](#edge-cases-and-pitfalls).

### Labeled child series

Constant labels apply to the whole family; dynamic labels select a child series
via `.labels(...)`:

```cpp
auto requests = reg->counter({.name   = "http_requests_total",
                              .help   = "HTTP requests",
                              .labels = prom::Labels{{"service", "api"}}});  // constant

requests.labels(prom::Labels{{"method", "GET"},  {"code", "200"}}).inc();
requests.labels(prom::Labels{{"method", "GET"},  {"code", "200"}}).inc();   // same child
requests.labels(prom::Labels{{"method", "POST"}, {"code", "500"}}).inc();
```

`prom::Labels` is kept sorted by name with duplicates collapsed (last write
wins), and caches an FNV-1a hash so it can key the backend's child cache.

> **Pitfall — labeled children are pinned.** A child snapshots its adapter (and
> scope-decorated state) at the moment `labels()` is called and **never
> migrates** if the adapter or scope later changes. Resolve children *after* the
> backend is installed.

### Per-library scopes (prefix + default labels)

A `Scope` is to `prom` what a named logger is to a logging library: one instance
per library, with a shared name prefix, default constant labels, and default
display metadata. It is registered process-wide by name.

```cpp
#include <prom/scope.hpp>

auto lib = prom::scope("mylib", {.prefix = "mylib_",
                                 .const_labels = prom::Labels{{"component", "io"}}});

auto c = lib->counter({.name = "requests_total", .help = "..."});
c.inc();   // exported as mylib_requests_total{component="io"}
```

The scope config is **live**, not copied at creation: changing the prefix or
default labels reconfigures every metric already created from the scope, and
subsequent samples flow to the newly-derived series.

```cpp
lib->set_prefix("srv_");          // existing metrics re-register under srv_*
lib->add_const_label("region", "eu");
```

A *user* of the library can fetch the same scope by name and adjust it:
`prom::scope("mylib")` returns the existing instance (the config argument is
ignored once a scope exists — reconfigure through the setters instead).

### Fanning out to several backends

`CompositeAdapter` forwards every call to a fixed list of child adapters —
useful for teeing metrics to two exporters during a migration, or feeding a real
backend and a test recorder at once:

```cpp
#include <prom/prom.hpp>

auto composite = std::make_shared<prom::CompositeAdapter>(
    std::vector<prom::AdapterPtr>{backend_a, backend_b});
prom::Registry::global()->set_adapter(composite);
```

Null entries in the list are dropped. The composite needs no locking of its own
(the list is fixed at construction), assuming each child honours the adapter
threading contract.

### Enumerating what has been declared

```cpp
for (const prom::MetricInfo& m : prom::Registry::global()->metrics()) {
    // m.type, m.name, m.help, m.const_labels, m.unit, m.scoped
}
for (const auto& s : prom::scopes()) { /* s->metrics() ... */ }
auto names = prom::scope_names();
```

`metrics()` returns read-only snapshots — including declared-but-not-yet-used
metrics — with a scope's effective name and labels computed live. The `Unit`
string views in a `MetricInfo` reference the live metric's storage, so use a
snapshot only while that metric is alive.

## Enabling a real backend

The prometheus-cpp adapter (`prom::prometheus_cpp`, gated by
`-DPROM_WITH_PROMETHEUS_CPP=ON`) is what a host installs to actually export:

```cpp
#include <prom/prom.hpp>
#include <prom/prometheus_cpp/adapter.hpp>

#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <iostream>
#include <memory>

int main() {
    // 1. Install the backend once, at startup.
    auto adapter = std::make_shared<prom::prometheus_cpp::PrometheusCppAdapter>();
    prom::Registry::global()->set_adapter(adapter);

    // 2. Library code records through prom exactly as before.
    auto reg      = prom::Registry::global();
    auto requests = reg->counter({.name   = "http_requests_total",
                                  .help   = "Total HTTP requests",
                                  .labels = prom::Labels{{"service", "api"}}});
    auto latency  = reg->histogram({.name = "http_request_seconds", .help = "Latency",
                                    .buckets = {0.05, 0.1, 0.25, 0.5, 1.0}});

    requests.inc();
    requests.labels(prom::Labels{{"code", "200"}}).inc(3);
    latency.observe(0.08);
    latency.observe(0.42);

    // 3. Expose adapter->registry() through an HTTP scrape endpoint, or render it:
    std::cout << prometheus::TextSerializer().Serialize(adapter->registry().Collect());
}
```

which yields standard Prometheus exposition text:

```
# HELP http_requests_total Total HTTP requests
# TYPE http_requests_total counter
http_requests_total{service="api",code="200"} 3
http_requests_total{service="api"} 1
# HELP http_request_seconds Latency
# TYPE http_request_seconds histogram
http_request_seconds_count 2
http_request_seconds_sum 0.5
http_request_seconds_bucket{le="0.05"} 0
http_request_seconds_bucket{le="0.1"} 1
...
```

**Type mapping.** Counter / Gauge / Histogram / Summary map to their direct
prometheus-cpp equivalents. `Untyped` maps to a Gauge. `Info` maps to a `<name>`
Gauge whose label set carries the payload at value `1`. `StateSet` maps to a
Gauge family with one series per state, each `0` or `1`.

Pass an existing registry to the adapter constructor
(`PrometheusCppAdapter{my_registry}`) when you already have one wired to a
`prometheus::Exposer`.

## Error handling

`prom` splits errors cleanly between *definition time* and *recording time*.

**Definition / registration validates.** Each `Registry` factory checks its
spec:

- The throwing factories (`counter`, `gauge`, ...) raise `prom::Exception` on a
  bad spec.
- The `noexcept` mirrors (`try_counter`, `try_gauge`, ...) return
  `prom::expected<T>` (an alias for `std::expected<T, prom::Error>`) instead.

```cpp
// Throwing form:
try {
    auto bad = prom::counter({.name = "9bad", .help = "h"});  // invalid name
} catch (const prom::Exception& e) {
    std::cerr << e.what() << '\n';                 // "invalid metric name: 9bad"
}

// noexcept form — no exceptions, inspect the result:
prom::expected<prom::Gauge> g = prom::try_gauge({.name = "9bad", .help = "h"});
if (!g) {
    // g.error().code == prom::ErrorCode::InvalidMetricName
    // g.error().message == "9bad"
}
```

What is validated:

- Metric name against `[a-zA-Z_][a-zA-Z0-9_]*` (`InvalidMetricName`).
- Label names, which additionally reject the reserved `__` prefix
  (`InvalidLabelName`).
- Histogram buckets must be non-empty, finite, and strictly increasing
  (`InvalidBuckets`).
- Summary quantiles must lie in the open interval `(0, 1)` (`InvalidQuantiles`).
- A state set must declare at least one state (`EmptyStateSet`).

**All mutations are `noexcept`.** Once a metric exists, recording never throws.
Invalid samples are *dropped and logged* (see below). The no-client path never
throws because a metric always resolves to at least the `NullAdapter`.

> Note: the `EmptyHelp` error code exists in the enum, but help text is **not**
> currently rejected by the factories — an empty `.help` is accepted (inferred
> from the validation code). Supply meaningful help text anyway; backends and
> dashboards rely on it.

## Edge cases and pitfalls

**Negative counter increment.** Dropped and logged; the counter stays
monotonic.

```cpp
auto c = prom::counter({.name = "x_total", .help = "h"});
c.inc(-5);   // no-op, logs a warning. c.inc(5) is fine.
```

**Non-finite sample (NaN / Inf).** Dropped and logged on any
`inc`/`set`/`dec`/`observe`.

**Unit-kind mismatch.** The first dimensional sample latches the metric's unit
(name, kind, symbol). A later dimensional sample of a *different kind* is dropped
and logged — never thrown. Declare a unit in the spec if you need it fixed up
front.

**Adapter swap orphans old series.** `set_adapter(...)` re-registers each metric
against the new backend on its *next use*, but series already written to the
previous backend stay there (backends cannot move a registered series). **Install
the backend before the bulk of your samples flow.**

**Labeled children do not migrate.** A child created with `labels()` pins its
binding at creation. If you swap the adapter (or reconfigure the scope)
afterward, the existing child keeps recording to the old binding. Re-resolve
children after the swap.

**Standalone metric used before the backend is installed.** It binds to the
`NullAdapter` on first use, then re-binds to the real backend on the next use
after `set_adapter(...)` — but anything recorded in between went to the
`NullAdapter` and is lost. Again: install early.

**`set_unit` is best-effort.** The prometheus-cpp backend cannot rename an
already-registered family, so late (inferred) units affect `prom`'s own
bookkeeping and the sample-dropping kind reconciliation, **not** the exported
series name. If you want the unit in the name, declare it in the spec.

**Duplicate labels collapse.** `prom::Labels{{"k","a"},{"k","b"}}` keeps only
`k="b"` (last write wins), and labels are always sorted by name.

**`MetricInfo` lifetime.** The `Unit` string views in an enumeration snapshot
point into the live metric. Don't keep a snapshot past the metric's lifetime.

## API overview

A compact map of the public surface a typical user reaches for first.

| API                                                                 | Purpose                                              | Notes                                       |
|---------------------------------------------------------------------|------------------------------------------------------|---------------------------------------------|
| `prom::counter/gauge/histogram/summary/untyped/info/stateset(spec)` | Create a metric via the global registry              | Throws `prom::Exception` on a bad spec.     |
| `prom::try_counter(...)` (and the rest)                             | `noexcept` mirrors                                   | Return `prom::expected<T>`.                 |
| `prom::Registry::global()`                                          | Process-wide registry                                | Shares the global adapter cell.             |
| `prom::Registry::create(adapter)`                                   | An independent registry with its own cell            | For tests / embedders.                      |
| `prom::Registry::create(adapter, config)`                           | A registry that decorates its metrics                | Live prefix / labels / display, like a scope. |
| `Registry::set_adapter(ptr)`                                        | Install / swap the backend (or reset with `nullptr`) | Existing metrics re-register on next use.   |
| `Registry::metrics()`                                               | Enumerate declared metrics as `MetricInfo`           | Includes unused ones.                       |
| `prom::scope(name[, config])`                                       | Get-or-create a named per-library scope              | Live, reconfigurable prefix/labels/display. |
| `prom::scopes()` / `scope_names()` / `find_scope(name)`             | Enumerate / look up scopes                           | `find_scope` returns `nullptr` if absent.   |
| `Metric::inc/dec/set/observe(...)`                                  | Record a sample                                      | `noexcept`; raw or dimensional value.       |
| `Metric::labels(Labels)`                                            | A same-type labeled child                            | Pinned binding (see pitfalls).              |
| `prom::Labels`                                                      | Sorted, deduped, hashed label set                    | `{{ "k", "v" }, ...}`.                      |
| `prom::Adapter`                                                     | The backend interface                                | Subclass to write a new backend.            |
| `prom::NullAdapter`                                                 | The default no-op backend                            | Always available, thread-safe.              |
| `prom::CompositeAdapter`                                            | Fan out to several backends                          | Fixed child list.                           |
| `prom::prometheus_cpp::PrometheusCppAdapter`                        | The prometheus-cpp backend                           | Opt-in module.                              |

## Examples

All examples live in `examples/` and build against the `NullAdapter` (no backend
needed), except the prometheus-cpp one.

| Example                                                   | Demonstrates                                                              |
|-----------------------------------------------------------|---------------------------------------------------------------------------|
| `examples/null_only.cpp`                                  | The minimal program: metrics with only the `NullAdapter`. Start here.     |
| `examples/library_metrics.cpp`                            | The headline use case: a library instrumented with no backend dependency. |
| `examples/raw_values.cpp`                                 | Recording plain arithmetic values of various types.                       |
| `examples/dimval_values.cpp`                              | Recording dimensional (dimval) values; unit inference.                    |
| `examples/labeled.cpp`                                    | Labeled child series from one family.                                     |
| `examples/all_metric_types.cpp`                           | Exercises every metric type once.                                         |
| `adapters/prometheus_cpp/examples/prometheus_backend.cpp` | Installs the real backend and prints scrape text.                         |

## Testing

The test suite uses GoogleTest (fetched automatically). The convenience
`Makefile` wraps the common CMake/CTest invocations:

```bash
make test          # configure + build + run core + NullAdapter tests
make prometheus    # same, with -DPROM_WITH_PROMETHEUS_CPP=ON (real backend + scrape tests)
make examples      # build and run every example, asserting exit 0
make sanitize      # ASan + UBSan
make release       # Release build + tests
make tidy          # clang-tidy
make format-check  # clang-format --dry-run --Werror
make ci            # the full pre-push gate (all of the above)
```

Or drive CMake directly:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### CMake options

| Option                     | Default   | Meaning                                   |
|----------------------------|-----------|-------------------------------------------|
| `PROM_BUILD_TESTS`         | top-level | Build the GoogleTest suite.               |
| `PROM_BUILD_EXAMPLES`      | top-level | Build the examples.                       |
| `PROM_BUILD_DOCS`          | `OFF`     | Build Doxygen HTML.                       |
| `PROM_ENABLE_CLANG_TIDY`   | `OFF`     | Run clang-tidy during the build.          |
| `PROM_ENABLE_SANITIZERS`   | `OFF`     | ASan + UBSan (Debug).                     |
| `PROM_ENABLE_COVERAGE`     | `OFF`     | Clang source-based coverage.              |
| `PROM_WARNINGS_AS_ERRORS`  | top-level | `-Werror` / `/WX`.                        |
| `PROM_INSTALL`             | top-level | Generate install/export rules.            |
| `PROM_WITH_PROMETHEUS_CPP` | `OFF`     | Build the prometheus-cpp backend adapter. |

## FAQ

**Do I need to link a library, or is it header-only?** The core is header-only —
link the `prom::prom` interface target (which carries the include paths and its
header-only dependencies). The optional prometheus-cpp adapter
(`prom::prometheus_cpp`) is a compiled static library.

**What happens if I record an invalid sample?** Mutations never throw. A negative
counter increment, a NaN/Inf value, or a unit-kind mismatch is dropped and
logged. Only *definition* (creating a metric with a bad spec) can fail — and you
choose throwing (`counter`) or `expected`-returning (`try_counter`) factories.

**Can I use this from multiple threads?** Yes. Metric mutation and binding are
safe from multiple threads; adapter access on a cell is mutex-guarded and hands
out a `shared_ptr` copy so a concurrent swap can't invalidate an in-flight
caller. Backends self-synchronize (`NullAdapter` is stateless; the
prometheus-cpp adapter guards family creation and relies on prometheus-cpp's
atomic series).

**Does a metric own its data or borrow it?** A metric owns its series state via a
`shared_ptr`; copies share it. The transient `MetricMeta`/`MetricInfo` views and
`Unit` string views *borrow* from that state and are only valid while it lives.

**What if no backend is ever installed?** Everything works as a logged no-op
through the `NullAdapter`. Nothing is exported, nothing throws.

**How do I see the no-op logging?** It goes through `logman`/spdlog under the
`prom`, `prom.null`, and `prom.composite` channels (registration at debug,
mutations at trace). Configure your logman/spdlog level to surface it.

**Which compiler versions work?** Not formally documented. The code requires
C++23 and is built with GCC and Apple Clang/libc++ per the build files. Treat
specific versions as inferred.

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

