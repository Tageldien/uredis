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
    if (!c)
    {
        const auto& err = c.error();
        usub::ulog::error("example_single: connect failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    co_await client.set("foo", "bar");
    auto g = co_await client.get("foo");

    if (g && g.value().has_value())
    {
        usub::ulog::info("example_single: GET foo -> '{}'", g.value().value());
    }

    usub::ulog::info("example_single: done");
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

    usub::ulog::info("main(single): starting uvent");

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(example_single());
    uvent.run();

    usub::ulog::info("main(single): uvent stopped");
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

    auto r = co_await pool.command("INCRBY", "counter", "1");
    if (!r)
    {
        const auto& err = r.error();
        usub::ulog::error("example_pool: INCRBY failed, category={}, message={}",
                          static_cast<int>(err.category), err.message);
        co_return;
    }

    const RedisValue& v = *r;
    if (v.is_integer())
    {
        usub::ulog::info("example_pool: counter -> {}", v.as_integer());
    }
    else
    {
        usub::ulog::warn("example_pool: INCRBY returned non-integer reply");
    }

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

## Pub/Sub (low-level)

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisSubscriber.h"
#include "uredis/RedisClient.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task   = usub::uvent::task;

using usub::ulog::info;
using usub::ulog::warn;
using usub::ulog::error;

static std::shared_ptr<RedisSubscriber> g_subscriber;

task::Awaitable<void> subscriber_coro()
{
    info("subscriber_coro: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    g_subscriber = std::make_shared<RedisSubscriber>(cfg);

    auto c = co_await g_subscriber->connect();
    if (!c)
    {
        const auto& err = c.error();
        error("subscriber_coro: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("subscriber_coro: connected");

    // SUBSCRIBE events
    auto r1 = co_await g_subscriber->subscribe(
        "events",
        [](const std::string& channel, const std::string& payload)
        {
            std::printf("[SUB] channel='%s' payload='%s'\n",
                        channel.c_str(), payload.c_str());
        });
    if (!r1)
    {
        const auto& err = r1.error();
        error("subscriber_coro: SUBSCRIBE events failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("subscriber_coro: subscribed to 'events'");

    // PSUBSCRIBE events.*
    auto r2 = co_await g_subscriber->psubscribe(
        "events.*",
        [](const std::string& channel, const std::string& payload)
        {
            std::printf("[PSUB] channel='%s' payload='%s'\n",
                        channel.c_str(), payload.c_str());
        });
    if (!r2)
    {
        const auto& err = r2.error();
        error("subscriber_coro: PSUBSCRIBE events.* failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("subscriber_coro: psubscribed to 'events.*'");

    info("subscriber_coro: waiting for messages...");
    using namespace std::chrono_literals;
    while (true)
    {
        co_await system::this_coroutine::sleep_for(1s);
    }
    co_return;
}

task::Awaitable<void> publisher_coro()
{
    info("publisher_coro: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        error("publisher_coro: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("publisher_coro: connected");

    using namespace std::chrono_literals;

    for (int i = 1; i <= 5; ++i)
    {
        std::string payload = "event_" + std::to_string(i);

        std::string_view args_arr[2] = {"events", payload};
        auto resp = co_await client.command(
            "PUBLISH",
            std::span<const std::string_view>(args_arr, 2)
        );
        if (!resp)
        {
            const auto& err = resp.error();
            error("publisher_coro: PUBLISH failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }

        const RedisValue& v = *resp;
        if (v.is_integer())
        {
            info("publisher_coro: PUBLISH events '{}' -> {} subscribers",
                 payload, v.as_integer());
        }
        else
        {
            warn("publisher_coro: PUBLISH events '{}' -> unexpected reply type", payload);
        }

        co_await system::this_coroutine::sleep_for(500ms);
    }

    info("publisher_coro: done");
    co_return;
}

task::Awaitable<void> control_coro()
{
    using namespace std::chrono_literals;

    info("control_coro: waiting before unsubscribe...");
    co_await system::this_coroutine::sleep_for(3s);

    if (!g_subscriber)
    {
        warn("control_coro: subscriber not initialized");
        co_return;
    }

    // UNSUBSCRIBE events
    {
        auto r = co_await g_subscriber->unsubscribe("events");
        if (!r)
        {
            const auto& err = r.error();
            error("control_coro: UNSUBSCRIBE events failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
        }
        else
        {
            info("control_coro: UNSUBSCRIBE events ok");
        }
    }

    // PUNSUBSCRIBE events.*
    {
        auto r = co_await g_subscriber->punsubscribe("events.*");
        if (!r)
        {
            const auto& err = r.error();
            error("control_coro: PUNSUBSCRIBE events.* failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
        }
        else
        {
            info("control_coro: PUNSUBSCRIBE events.* ok");
        }
    }

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

    info("main(pubsub): starting uvent");

    usub::Uvent uvent(3);
    system::co_spawn(subscriber_coro());
    system::co_spawn(publisher_coro());
    system::co_spawn(control_coro());
    uvent.run();

    info("main(pubsub): uvent stopped");
    return 0;
}
```

---

## RedisBus

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisBus.h"
#include <ulog/ulog.h>

// See the RedisBus section for full example:
//  - RedisBus::run() in a background coroutine
//  - user coroutine with subscribe/psubscribe and publish calls.
```

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

    info("main(reflect): starting uvent");

    usub::Uvent uvent(2);
    usub::uvent::system::co_spawn(reflect_example());
    uvent.run();

    info("main(reflect): uvent stopped");
    return 0;
}
```