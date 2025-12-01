#include "uvent/Uvent.h"
#include "uredis/RedisBus.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task   = usub::uvent::task;

using usub::ulog::info;
using usub::ulog::warn;
using usub::ulog::error;

task::Awaitable<void> bus_user_coro(RedisBus& bus)
{
    info("bus_user_coro: start");

    auto r1 = co_await bus.subscribe(
        "events",
        [](const std::string& ch, const std::string& payload)
        {
            std::printf("[BUS SUB] %s => %s\n", ch.c_str(), payload.c_str());
        });
    if (!r1)
    {
        const auto& err = r1.error();
        error("bus_user_coro: subscribe events failed: {}", err.message);
        co_await bus.close();
        co_return;
    }

    auto r2 = co_await bus.psubscribe(
        "events.*",
        [](const std::string& ch, const std::string& payload)
        {
            std::printf("[BUS PSUB] %s => %s\n", ch.c_str(), payload.c_str());
        });
    if (!r2)
    {
        const auto& err = r2.error();
        error("bus_user_coro: psubscribe events.* failed: {}", err.message);
        co_await bus.close();
        co_return;
    }

    info("bus_user_coro: subscriptions set (events, events.*)");

    using namespace std::chrono_literals;

    for (int i = 0; i < 5; ++i)
    {
        std::string payload = "msg_" + std::to_string(i);
        auto pr = co_await bus.publish("events", payload);
        if (!pr)
        {
            const auto& err = pr.error();
            error("bus_user_coro: publish failed: {}", err.message);
            co_await bus.close();
            co_return;
        }
        info("bus_user_coro: PUBLISH events '{}'", payload);
        co_await system::this_coroutine::sleep_for(500ms);
    }

    co_await system::this_coroutine::sleep_for(2s);

    info("bus_user_coro: closing bus");
    co_await bus.close();
    info("bus_user_coro: done");
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

    info("main(bus): starting uvent");

    RedisBus::Config cfg;
    cfg.redis.host        = "127.0.0.1";
    cfg.redis.port        = 15100;
    cfg.ping_interval_ms  = 3000;
    cfg.reconnect_delay_ms = 1000;

    RedisBus bus{cfg};

    usub::Uvent uvent(4);

    system::co_spawn(bus.run());
    system::co_spawn(bus_user_coro(bus));

    uvent.run();

    info("main(bus): uvent stopped");
    return 0;
}
