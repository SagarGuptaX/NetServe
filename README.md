# NetServe

A concurrent HTTP server built from scratch in C++ using POSIX sockets and a hand-rolled thread pool — no frameworks, no Boost, no libuv. NetServe demonstrates the mechanics at the foundation of every backend service: how connections are accepted, how work is scheduled across threads, and how computation is exposed as an HTTP API.

---

## What it demonstrates

Most backend engineers use HTTP frameworks without understanding what sits beneath them. NetServe is built at the layer those frameworks abstract away:

- **Raw socket programming** — the full POSIX lifecycle: `socket → bind → listen → accept → recv → send → close`, written directly without abstraction.
- **Thread pool from scratch** — a fixed worker pool backed by a `std::queue`, `std::mutex`, and `std::condition_variable`. The main thread accepts connections and enqueues tasks; workers execute them concurrently. No `std::async`, no external library.
- **Decoupled concurrency** — request intake and request execution are separated. The accept loop never blocks on processing, which is the core property that makes servers scale.
- **Process orchestration** — the `/run-cache-sim` endpoint shells out to an external binary via `popen()`, captures its stdout, parses the result, and returns structured JSON. This demonstrates how HTTP services compose with other systems.
- **Layered architecture** — network, concurrency, HTTP parsing, and routing are kept in separate modules with no cross-layer coupling. Each layer has one job.

---

## Architecture

```
Client
  │
  ▼
accept()          ← main thread only, never blocks on work
  │
  ▼
ThreadPool        ← bounded worker threads, task queue with mutex + CV
  │
  ▼
HTTP Parser       ← method, path, query params extracted from raw bytes
  │
  ▼
Router            ← /health  or  /run-cache-sim
  │
  ▼
Handler           ← popen() → external binary → stdout → JSON response
  │
  ▼
send()            ← HTTP/1.1 wire response
```

The key design decision is the separation between accepting connections and processing them. Without a thread pool, the server handles one request at a time — `accept → process → respond → accept next`. With the thread pool, the pattern becomes `accept → enqueue → accept next immediately`, while workers handle the queued tasks in parallel.

---

## Project structure

```
src/
  main.cpp                  — CLI parsing, path validation, server startup
  net/
    server.h / server.cpp   — socket lifecycle, event loop, routing, handlers
  concurrency/
    thread_pool.h / .cpp    — worker threads, task queue, graceful shutdown
  http/
    http_message.h          — HttpRequest / HttpResponse structs
    http_parser.h / .cpp    — request parsing, response serialisation
```

---

## Build

Requires a C++17 compiler and CMake 3.16+. Tested on Linux (GCC 13).

```bash
cmake -S . -B build
cmake --build build
```

The binary is placed at the project root as `./netserve`.

---

## Usage

```bash
./netserve <port> [thread_count] --cache-audit <path> --traces-dir <path>
```

`--cache-audit` and `--traces-dir` are validated at startup. If either path does not exist, the server exits before binding the socket — misconfiguration is caught immediately, not at request time.

```bash
# Minimal — uses hardware_concurrency() threads
./netserve 8080 \
  --cache-audit ../CacheAudit/cache_audit \
  --traces-dir  ../CacheAudit/traces/synthetic

# Explicit thread count
./netserve 8080 4 \
  --cache-audit ./cache_audit \
  --traces-dir  ./traces/synthetic
```

---

## Endpoints

### `GET /health`

Returns `200 OK` with plain-text body `OK`. Exists to confirm the server is alive and the socket is accepting connections.

```bash
curl http://localhost:8080/health
# OK
```

### `GET /run-cache-sim`

Runs a cache simulation by invoking the configured external binary, capturing its output, and returning structured JSON.

**Query parameters:**

| Parameter    | Required | Default | Values |
|---|---|---|---|
| `policy`     | yes      | —       | `fifo` `lru` `lfu` `arc` `belady` |
| `cache_size` | no       | `32`    | any positive integer |
| `trace`      | no       | `loop`  | `loop` `scan` `skewed` `hot_cold` |

```bash
# Defaults
curl "http://localhost:8080/run-cache-sim?policy=lru"
# {"status":"ok","policy":"lru","cache_size":32,"trace":"loop","hit_rate":0.9500,"hits":570,"misses":30,"runtime_ms":0}

# Full parameters
curl "http://localhost:8080/run-cache-sim?policy=arc&trace=hot_cold&cache_size=10"
# {"status":"ok","policy":"arc","cache_size":10,"trace":"hot_cold","hit_rate":0.2667,"hits":220,"misses":605,"runtime_ms":0}
```

**Error responses:**

| Case | Status |
|---|---|
| Missing `policy` | `400` with JSON error |
| Unknown `policy` or `trace` value | `400` with JSON error |
| Invalid `cache_size` (non-integer or ≤ 0) | `400` with JSON error |
| External binary fails | `500` with JSON error |
| Unknown path | `404` |
| Non-GET method | `405` |

---

## Adapting to a different backend

NetServe is not a CacheAudit-specific tool. The socket layer, thread pool, and HTTP parser are completely generic. The only coupling to CacheAudit lives in two places inside `src/net/server.cpp`:

**1. The command string** (in `handle_cache_sim`):
```cpp
std::string cmd =
    cache_audit_bin_ + " " + trace_path + " " + policy + " " +
    std::to_string(cache_size) + " 2>/dev/null";
```
Replace this with whatever command your backend binary expects.

**2. The output parser** (in `handle_cache_sim`):
```cpp
std::string hit_rate_raw = find_field(output, "Hit Rate:");
std::string hits_raw     = find_field(output, "Hits:");
```
`find_field()` is a simple line scanner — replace the field labels with whatever your binary prints to stdout.

Everything else — the thread pool, the socket loop, the HTTP parsing, the routing structure — requires no changes. A different backend binary is a drop-in replacement.

---

## Thread pool internals

The pool is constructed with N threads. Each worker runs this loop:

```cpp
while (true) {
    std::function<void()> task;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] { return stop_ || !task_queue_.empty(); });
        if (stop_ && task_queue_.empty()) return;
        task = std::move(task_queue_.front());
        task_queue_.pop();
    }
    task(); // executed outside the lock
}
```

Workers sleep on the condition variable and wake only when a task is available or shutdown is signalled. The task is moved out of the queue before the lock is released, so execution happens without holding the mutex — other workers can dequeue concurrently. On destruction, `stop_` is set to true, all workers are broadcast to, and each thread is `join()`ed. No in-flight task is dropped.

---

## Known limitations

`popen()` is synchronous — a worker thread is held for the full duration of the external process. Under high concurrency with slow backend runs, the thread pool could saturate. The correct fix is async process management (non-blocking `fork`/`exec` with `epoll`), which is deliberately out of scope here. In practice, for the workloads this project targets, the pool size is the tuning knob — passing a higher `thread_count` at startup directly controls how many concurrent backend calls can run.

---

## Related

[CacheAudit](https://github.com/SagarGuptaX/CacheAudit) — the cache simulation engine that NetServe wraps in this configuration. CacheAudit is a standalone trace-driven benchmarking framework; NetServe exposes it as a concurrent HTTP service.
