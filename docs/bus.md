# RedisBus

`RedisBus` is a higher-level abstraction built on top of `RedisClient` and `RedisSubscriber`.

It provides:

- Single **publish** connection.
- Separate **subscribe** connection.
- Auto-reconnect loop with periodic `PING`.
- Automatic resubscription to previously requested channels/patterns.
- Callbacks on errors and reconnects.

```cpp
class RedisBus
{
public:
    using Callback = std::function<void(const std::string& channel,
                                        const std::string& payload)>;

    struct Config
    {
        RedisConfig redis;
        int ping_interval_ms{5000};
        int reconnect_delay_ms{2000};

        std::function<void(const RedisError&)> on_error;
        std::function<void()> on_reconnect;
    };

    explicit RedisBus(Config cfg);

    task::Awaitable<void> run();

    task::Awaitable<RedisResult<void>> publish(
        std::string_view channel,
        std::string_view payload);

    task::Awaitable<RedisResult<void>> subscribe(
        std::string channel,
        Callback cb);

    task::Awaitable<RedisResult<void>> psubscribe(
        std::string pattern,
        Callback cb);

    task::Awaitable<RedisResult<void>> unsubscribe(std::string channel);
    task::Awaitable<RedisResult<void>> punsubscribe(std::string pattern);

    task::Awaitable<void> close();
};
```

## Typical usage

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisBus.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

task::Awaitable<void> bus_user_coro(RedisBus& bus)
{
    auto r1 = co_await bus.subscribe(
        "events",
        [](const std::string& ch, const std::string& payload) {
            std::printf("[BUS SUB] %s => %s\n", ch.c_str(), payload.c_str());
        });

    auto r2 = co_await bus.psubscribe(
        "events.*",
        [](const std::string& ch, const std::string& payload) {
            std::printf("[BUS PSUB] %s => %s\n", ch.c_str(), payload.c_str());
        });

    using namespace std::chrono_literals;

    for (int i = 0; i < 5; ++i)
    {
        std::string payload = "msg_" + std::to_string(i);
        co_await bus.publish("events", payload);
        co_await system::this_coroutine::sleep_for(500ms);
    }

    co_await system::this_coroutine::sleep_for(2s);
    co_await bus.close();
    co_return;
}

int main()
{
    RedisBus::Config cfg;
    cfg.redis.host         = "127.0.0.1";
    cfg.redis.port         = 15100;
    cfg.ping_interval_ms   = 3000;
    cfg.reconnect_delay_ms = 1000;

    RedisBus bus{cfg};

    usub::Uvent uvent(4);
    usub::uvent::system::co_spawn(bus.run());
    usub::uvent::system::co_spawn(bus_user_coro(bus));
    uvent.run();
}
```

## Connection management

* `run()` owns the reconnect loop:

    * If not connected, it calls `ensure_connected_locked()`.
    * On success, it calls `resubscribe_all_locked()` to restore all desired channels/patterns.
    * Periodically sends `PING` via pub client.
    * On failures, calls `on_error` and sleeps `reconnect_delay_ms`.

* `subscribe` / `psubscribe`:

    * Update `desired_channels_` / `desired_patterns_`.
    * Ensure bus is connected.
    * Call subscriber’s `subscribe()` / `psubscribe()`.

* `unsubscribe` / `punsubscribe`:

    * Remove from desired sets.
    * If currently connected, perform actual `UNSUBSCRIBE`/`PUNSUBSCRIBE`.

Use `close()` to stop the loop and shut down underlying clients.