# uRedis

uRedis is a small async Redis client library built on top of
[uvent](https://github.com/Usub-development/uvent).

It gives you:

- `RedisClient` – single async connection with a small typed API.
- `RedisPool` – round-robin pool of `RedisClient` instances.
- `RedisSubscriber` – low-level SUBSCRIBE/PSUBSCRIBE client.
- `RedisBus` – high-level resilient pub/sub bus with auto-reconnect.
- `RedisValue` / `RedisResult` / `RedisError` – result and error types.
- `RespParser` – incremental RESP parser.
- `reflect` helpers – mapping C++ aggregates to Redis hashes using **ureflect**.

Documentation: **https://usub-development.github.io/uredis/**

---

## Features

- Async, coroutine-based API (`task::Awaitable<>`).
- RESP parser with incremental feeding.
- Simple error model:
  - `Io` – connection / timeout / write/read failures
  - `Protocol` – malformed / unexpected reply
  - `ServerReply` – `-ERR ...` from Redis
- Pub/Sub:
  - raw `RedisSubscriber`
  - higher-level `RedisBus` with:
    - separate pub/sub connections
    - periodic `PING`
    - reconnect loop and resubscription
- Reflection helpers for `HSET` / `HGETALL` with aggregates.

---

## Requirements

- C++23
- [uvent](https://github.com/Usub-development/uvent)
- [ulog](https://github.com/Usub-development/ulog) (for logging) (optional)
- [ureflect](https://github.com/Usub-development/ureflect) (for reflection helpers)

Redis server 6+ is recommended but not strictly required.

---

## Building / Integration

### FetchContent (example)

```cmake
include(FetchContent)

FetchContent_Declare(
    uvent
    GIT_REPOSITORY https://github.com/Usub-development/uvent.git
    GIT_TAG main
)

FetchContent_Declare(
    ureflect
    GIT_REPOSITORY https://github.com/Usub-development/ureflect.git
    GIT_TAG main
)

FetchContent_Declare(
    uredis
    GIT_REPOSITORY https://github.com/Usub-development/uredis.git
    GIT_TAG main
)

FetchContent_MakeAvailable(uvent ureflect uredis)

add_executable(my_app main.cpp)

target_link_libraries(my_app
    PRIVATE
        uvent
        uredis
        ureflect
)
````

### Enable internal logs

```cmake
target_compile_definitions(uredis PUBLIC UREDIS_LOGS)
```

uRedis logging goes through `ulog`.

---

## Quick start

### Single client

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

    usub::ulog::info("redis_example: connecting to Redis {}:{}", cfg.host, cfg.port);

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        usub::ulog::error("redis_example: connect failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    usub::ulog::info("redis_example: connected");

    auto set_res = co_await client.set("foo", "bar");
    if (!set_res)
    {
        const auto& err = set_res.error();
        usub::ulog::error("redis_example: SET foo=bar failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }
    usub::ulog::info("redis_example: SET foo=bar ok");

    auto get_res = co_await client.get("foo");
    if (!get_res)
    {
        const auto& err = get_res.error();
        usub::ulog::error("redis_example: GET foo failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    if (get_res.value().has_value())
    {
        auto val = get_res.value().value();
        usub::ulog::info("redis_example: GET foo -> '{}'", val);
    }
    else
    {
        usub::ulog::warn("redis_example: GET foo -> (nil)");
    }

    usub::ulog::info("redis_example: done");
    co_return;
}

int main()
{
    usub::ulog::ULogInit log_cfg{
        .trace_path = nullptr,
        .debug_path = nullptr,
        .info_path = nullptr,
        .warn_path = nullptr,
        .error_path = nullptr,
        .flush_interval_ns = 2'000'000ULL,
        .queue_capacity = 16384,
        .batch_size = 512,
        .enable_color_stdout = true,
        .max_file_size_bytes = 10 * 1024 * 1024,
        .max_files = 3,
        .json_mode = false,
        .track_metrics = true
    };

    usub::ulog::init(log_cfg);

    usub::ulog::info("main: starting uvent");

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(redis_example());
    uvent.run();

    usub::ulog::info("main: uvent stopped");
    return 0;
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
    usub::ulog::info("example_pool: start");

    RedisPoolConfig pcfg;
    pcfg.host = "127.0.0.1";
    pcfg.port = 15100;
    pcfg.db   = 0;
    pcfg.size = 8;

    RedisPool pool{pcfg};
    auto rc = co_await pool.connect_all();
    if (!rc)
    {
        const auto& err = rc.error();
        usub::ulog::error("example_pool: connect_all failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }
    usub::ulog::info("example_pool: all clients connected");

    auto r = co_await pool.command("INCRBY", "counter", "1");
    if (!r)
    {
        const auto& err = r.error();
        usub::ulog::error("example_pool: INCRBY counter 1 failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    const RedisValue& v = *r;
    if (!v.is_integer())
    {
        usub::ulog::error("example_pool: INCRBY unexpected reply type");
        co_return;
    }

    usub::ulog::info("example_pool: INCRBY counter -> {}", v.as_integer());
    usub::ulog::info("example_pool: done");
    co_return;
}

int main()
{
    usub::ulog::ULogInit log_cfg{
        .trace_path = nullptr,
        .debug_path = nullptr,
        .info_path = nullptr,
        .warn_path = nullptr,
        .error_path = nullptr,
        .flush_interval_ns = 2'000'000ULL,
        .queue_capacity = 16384,
        .batch_size = 512,
        .enable_color_stdout = true,
        .max_file_size_bytes = 10 * 1024 * 1024,
        .max_files = 3,
        .json_mode = false,
        .track_metrics = true
    };

    usub::ulog::init(log_cfg);

    usub::ulog::info("main(pool): starting uvent");

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(example_pool());
    uvent.run();

    usub::ulog::info("main(pool): uvent stopped");
    return 0;
}
```

---

## Pub/Sub and RedisBus

For complete pub/sub and bus examples (including `RedisSubscriber`, `RedisBus`, `subscriber_coro`, `publisher_coro`, `control_coro`, and `bus_user_coro`), see the **Examples** and **Pub/Sub / RedisBus** sections in the docs:

* [https://usub-development.github.io/uredis/examples/#pubsub-low-level](https://usub-development.github.io/uredis/examples/#pubsub-low-level)

---

## Reflection helpers

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisReflect.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

using usub::ulog::info;
using usub::ulog::error;

struct User
{
    int64_t id;
    std::string name;
    bool active;
    std::optional<int64_t> age;
};

task::Awaitable<void> reflect_example()
{
    info("reflect_example: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        error("reflect_example: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("reflect_example: connected");

    using namespace usub::uredis::reflect;

    User u{.id = 42, .name = "Kirill", .active = true, .age = 30};

    auto hset_res = co_await hset_struct(client, "user:42", u);
    if (!hset_res)
    {
        const auto& err = hset_res.error();
        error("reflect_example: hset_struct failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("reflect_example: hset_struct user:42 fields={}", hset_res.value());

    auto loaded = co_await hget_struct<User>(client, "user:42");
    if (!loaded)
    {
        const auto& err = loaded.error();
        error("reflect_example: hget_struct failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }

    if (!loaded.value().has_value())
    {
        info("reflect_example: hget_struct user:42 -> (nil)");
    }
    else
    {
        const User& u2 = *loaded.value();
        info("reflect_example: hget_struct user:42 -> id={} name='{}' active={} age={}",
             u2.id,
             u2.name,
             u2.active,
             u2.age.has_value() ? std::to_string(*u2.age) : std::string("<null>"));
    }

    info("reflect_example: done");
    co_return;
}
```

---

# Licence

uredis is distributed under the [MIT license](LICENSE)