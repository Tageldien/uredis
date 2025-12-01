# uRedis

uRedis is a small, async-first Redis client library built on top of the **uvent** event loop.

It provides:

- `RedisClient` – single async connection with RESP parsing and a small typed API.
- `RedisPool` – round-robin pool of `RedisClient` instances.
- `RedisSubscriber` – low-level SUBSCRIBE / PSUBSCRIBE client.
- `RedisBus` – high-level resilient pub/sub bus with auto-reconnect and resubscription.
- `RedisValue` / `RedisResult` / `RedisError` – result and error types.
- `RespParser` – incremental RESP parser.
- `reflect` helpers – map C++ aggregates to Redis hashes (`HSET`/`HGETALL`) using **ureflect**.

The library is designed to be:

- **Async** – based on `usub::uvent::task::Awaitable`.
- **Zero external runtime deps** – only uvent/ulog/ureflect headers and your Redis server.
- **Minimal** – focuses on basic commands and primitives you actually use.

## Dependencies

- C++23
- [uvent](https://github.com/Usub-development/uvent) – event loop, sockets, coroutines
- [ulog](https://github.com/Usub-development/ulog) – logging (optional, behind `UREDIS_LOGS`)
- [ureflect](https://github.com/Usub-development/ureflect) – for reflection helpers only

## Basic example

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

task::Awaitable<void> redis_example()
{
    usub::ulog::info("redis_example: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        usub::ulog::error("connect failed: category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    auto set_res = co_await client.set("foo", "bar");
    auto get_res = co_await client.get("foo");

    if (get_res && get_res.value().has_value())
    {
        auto v = get_res.value().value();
        usub::ulog::info("GET foo -> '{}'", v);
    }

    co_return;
}

int main()
{
    usub::ulog::ULogInit log_cfg{ .enable_color_stdout = true };
    usub::ulog::init(log_cfg);

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(redis_example());
    uvent.run();
}
```

## Logging

If you compile with `UREDIS_LOGS` defined, uredis will emit debug/info/error logs via `ulog`.

```cmake
target_compile_definitions(uredis PUBLIC UREDIS_LOGS)
```

## docs/examples.md

```markdown
# Examples

This page collects complete runnable examples using different parts of uRedis.

---

## Single client

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

task::Awaitable<void> example_single()
{
    usub::ulog::info("example_single: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    auto c = co_await client.connect();

    co_await client.set("foo", "bar");
    auto g = co_await client.get("foo");

    if (g && g.value().has_value()) {
        usub::ulog::info("GET foo -> '{}'", g.value().value());
    }

    co_return;
}

int main()
{
    usub::ulog::ULogInit log_cfg{ .enable_color_stdout = true };
    usub::ulog::init(log_cfg);

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(example_single());
    uvent.run();
}
```

---

## Pool example

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisPool.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

task::Awaitable<void> example_pool()
{
    RedisPoolConfig pcfg;
    pcfg.host = "127.0.0.1";
    pcfg.port = 15100;
    pcfg.db   = 0;
    pcfg.size = 8;

    RedisPool pool{pcfg};
    auto rc = co_await pool.connect_all();

    auto r = co_await pool.command("INCRBY", "counter", "1");
    if (r && r->is_integer()) {
        usub::ulog::info("counter -> {}", r->as_integer());
    }

    co_return;
}
```

---

## Pub/Sub (low-level)

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisSubscriber.h"
#include "uredis/RedisClient.h"
#include <ulog/ulog.h>

// same as in Pub/Sub section: subscriber_coro / publisher_coro / control_coro
```

---

## RedisBus

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisBus.h"
#include <ulog/ulog.h>

// same as in RedisBus section: bus.run() + bus_user_coro(...)
```

---

## Reflection helpers

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisReflect.h"
#include <ulog/ulog.h>

struct User
{
    int64_t id;
    std::string name;
    bool active;
    std::optional<int64_t> age;
};

task::Awaitable<void> reflect_example()
{
    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    co_await client.connect();

    using namespace usub::uredis::reflect;

    User u{.id = 42, .name = "Kirill", .active = true, .age = 30};

    auto hset_res = co_await hset_struct(client, "user:42", u);
    auto loaded   = co_await hget_struct<User>(client, "user:42");

    co_return;
}
```