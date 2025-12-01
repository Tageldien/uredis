# Pub/Sub: RedisSubscriber

`RedisSubscriber` is a low-level async client for Redis pub/sub (SUBSCRIBE / PSUBSCRIBE).

```cpp
class RedisSubscriber
{
public:
    using MessageCallback = std::function<void(const std::string& channel,
                                               const std::string& payload)>;

    explicit RedisSubscriber(RedisConfig cfg);

    task::Awaitable<RedisResult<void>> connect();

    task::Awaitable<RedisResult<void>> subscribe(std::string channel, MessageCallback cb);
    task::Awaitable<RedisResult<void>> psubscribe(std::string pattern, MessageCallback cb);

    task::Awaitable<RedisResult<void>> unsubscribe(std::string channel);
    task::Awaitable<RedisResult<void>> punsubscribe(std::string pattern);

    task::Awaitable<void> close();

    bool is_connected() const;
};
```

Internally it:

* Uses `TCPClientSocket` from uvent.
* Encodes commands as RESP arrays (`SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE`, `PUNSUBSCRIBE`).
* Uses `RespParser` to parse incoming messages.
* Dispatches `message` / `pmessage` / subscribe events to callbacks.

## Simple pub/sub example

```cpp
#include "uvent/Uvent.h"
#include "uredis/RedisSubscriber.h"
#include "uredis/RedisClient.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

static std::shared_ptr<RedisSubscriber> g_subscriber;

task::Awaitable<void> subscriber_coro()
{
    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    g_subscriber = std::make_shared<RedisSubscriber>(cfg);

    auto c = co_await g_subscriber->connect();

    auto r1 = co_await g_subscriber->subscribe(
        "events",
        [](const std::string& ch, const std::string& payload) {
            std::printf("[SUB] %s => %s\n", ch.c_str(), payload.c_str());
        });

    auto r2 = co_await g_subscriber->psubscribe(
        "events.*",
        [](const std::string& ch, const std::string& payload) {
            std::printf("[PSUB] %s => %s\n", ch.c_str(), payload.c_str());
        });

    using namespace std::chrono_literals;
    while (true) {
        co_await system::this_coroutine::sleep_for(1s);
    }
}

task::Awaitable<void> publisher_coro()
{
    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    co_await client.connect();

    using namespace std::chrono_literals;

    for (int i = 1; i <= 5; ++i)
    {
        std::string payload = "event_" + std::to_string(i);
        std::string_view args_arr[2] = {"events", payload};
        co_await client.command("PUBLISH",
            std::span<const std::string_view>(args_arr, 2));

        co_await system::this_coroutine::sleep_for(500ms);
    }
    co_return;
}

int main()
{
    usub::Uvent uvent(3);
    system::co_spawn(subscriber_coro());
    system::co_spawn(publisher_coro());
    uvent.run();
}
```

## Unsubscribe

```cpp
auto r = co_await g_subscriber->unsubscribe("events");
auto rp = co_await g_subscriber->punsubscribe("events.*");
```

On IO failures the subscriber:

* Stops `reader_loop`.
* Marks itself as disconnected.
* Fails all pending subscribe/unsubscribe futures with an error.