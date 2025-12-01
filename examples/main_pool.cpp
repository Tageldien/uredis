#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisPool.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

using usub::ulog::info;
using usub::ulog::warn;
using usub::ulog::error;

task::Awaitable<void> example_single()
{
    info("example_single: start");

    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;
    cfg.db = 0;

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        error("example_single: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("example_single: connected");

    auto r1 = co_await client.set("foo", "bar");
    if (!r1)
    {
        const auto& err = r1.error();
        error("example_single: SET foo=bar failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("example_single: SET foo=bar ok");

    auto g = co_await client.get("foo");
    if (!g)
    {
        const auto& err = g.error();
        error("example_single: GET foo failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }

    if (g.value().has_value())
    {
        auto v = g.value().value();
        info("example_single: GET foo -> '{}'", v);
    }
    else
    {
        warn("example_single: GET foo -> (nil)");
    }

    info("example_single: done");
    co_return;
}

task::Awaitable<void> example_pool()
{
    info("example_pool: start");

    RedisPoolConfig pcfg;
    pcfg.host = "127.0.0.1";
    pcfg.port = 15100;
    pcfg.db = 0;
    pcfg.size = 8;

    RedisPool pool{pcfg};
    auto rc = co_await pool.connect_all();
    if (!rc)
    {
        const auto& err = rc.error();
        error("example_pool: connect_all failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("example_pool: all clients connected");

    auto r = co_await pool.command("INCRBY", "counter", "1");
    if (!r)
    {
        const auto& err = r.error();
        error("example_pool: INCRBY counter 1 failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }

    const RedisValue& v = *r;
    if (!v.is_integer())
    {
        error("example_pool: INCRBY unexpected reply type");
        co_return;
    }

    info("example_pool: INCRBY counter -> {}", v.as_integer());
    info("example_pool: done");
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

    info("main(pool): starting uvent");

    usub::Uvent uvent(4);
    system::co_spawn(example_single());
    system::co_spawn(example_pool());
    uvent.run();

    info("main(pool): uvent stopped");
    return 0;
}