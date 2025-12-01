#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisReflect.h"
#include <ulog/ulog.h>

using namespace usub::uvent;
using namespace usub::uredis;
namespace task = usub::uvent::task;

using usub::ulog::info;
using usub::ulog::warn;
using usub::ulog::error;

struct User
{
    int64_t id;
    std::string name;
    bool active;
    std::optional<int64_t> age;
};

task::Awaitable<void> redis_example()
{
    info("redis_example: start");

    RedisConfig cfg;
    cfg.host = "localhost";
    cfg.port = 15100;

    info("redis_example: connecting to Redis {}:{}", cfg.host, cfg.port);

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        error("redis_example: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }

    info("redis_example: connected");

    auto set_res = co_await client.set("foo", "bar");
    if (!set_res)
    {
        const auto& err = set_res.error();
        error("redis_example: SET foo=bar failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("redis_example: SET foo=bar ok");

    auto get_res = co_await client.get("foo");
    if (!get_res)
    {
        const auto& err = get_res.error();
        error("redis_example: GET foo failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }

    if (get_res.value().has_value())
    {
        auto val = get_res.value().value();
        info("redis_example: GET foo -> '{}'", val);
    }
    else
    {
        warn("redis_example: GET foo -> (nil)");
    }

    info("redis_example: done");
    co_return;
}

task::Awaitable<void> redis_structs_example()
{
    info("redis_structs_example: start");

    RedisConfig cfg;
    cfg.host = "localhost";
    cfg.port = 15100;

    info("redis_structs_example: connecting to Redis {}:{}", cfg.host, cfg.port);

    RedisClient client{cfg};
    auto c = co_await client.connect();
    if (!c)
    {
        const auto& err = c.error();
        error("redis_structs_example: connect failed, category={}, message={}",
              static_cast<int>(err.category), err.message);
        co_return;
    }
    info("redis_structs_example: connected");

    // ===== HASH (ручной) =====
    {
        auto r1 = co_await client.hset("user:1", "name", "Kirill");
        if (!r1)
        {
            const auto& err = r1.error();
            error("redis_structs_example: HSET user:1 name failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }

        auto r2 = co_await client.hset("user:1", "role", "admin");
        if (!r2)
        {
            const auto& err = r2.error();
            error("redis_structs_example: HSET user:1 role failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }

        info("redis_structs_example: HSET user:1 name/role ok");

        auto name = co_await client.hget("user:1", "name");
        if (!name)
        {
            const auto& err = name.error();
            error("redis_structs_example: HGET user:1 name failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        if (name.value().has_value())
        {
            info("redis_structs_example: HGET user:1 name -> '{}'",
                 name.value().value());
        }
        else
        {
            warn("redis_structs_example: HGET user:1 name -> (nil)");
        }

        auto all = co_await client.hgetall("user:1");
        if (!all)
        {
            const auto& err = all.error();
            error("redis_structs_example: HGETALL user:1 failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: HGETALL user:1 size={}", all.value().size());
    }

    // ===== SET =====
    {
        std::string_view tags_arr[] = {"foo", "bar", "baz"};
        auto r = co_await client.sadd("tags",
                                      std::span<const std::string_view>(tags_arr, 3));
        if (!r)
        {
            const auto& err = r.error();
            error("redis_structs_example: SADD tags failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: SADD tags count={}", r.value());

        auto tags = co_await client.smembers("tags");
        if (!tags)
        {
            const auto& err = tags.error();
            error("redis_structs_example: SMEMBERS tags failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: SMEMBERS tags size={}", tags.value().size());
    }

    // ===== LIST =====
    {
        std::string_view q_arr[] = {"job1", "job2", "job3"};
        auto r = co_await client.lpush("queue",
                                       std::span<const std::string_view>(q_arr, 3));
        if (!r)
        {
            const auto& err = r.error();
            error("redis_structs_example: LPUSH queue failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: LPUSH queue new_len={}", r.value());

        auto jobs = co_await client.lrange("queue", 0, -1);
        if (!jobs)
        {
            const auto& err = jobs.error();
            error("redis_structs_example: LRANGE queue failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: LRANGE queue size={}", jobs.value().size());
    }

    // ===== ZSET =====
    {
        std::pair<std::string, double> members[] = {
            {"user1", 10.0},
            {"user2", 20.0},
        };
        auto r = co_await client.zadd(
            "scores",
            std::span<const std::pair<std::string, double>>(members, 2));
        if (!r)
        {
            const auto& err = r.error();
            error("redis_structs_example: ZADD scores failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: ZADD scores added={}", r.value());

        auto ranking = co_await client.zrange_with_scores("scores", 0, -1);
        if (!ranking)
        {
            const auto& err = ranking.error();
            error("redis_structs_example: ZRANGE WITHSCORES scores failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: ZRANGE WITHSCORES scores size={}", ranking.value().size());
    }

    // ===== REFLECT: hset_struct / hget_struct =====
    {
        using namespace usub::uredis::reflect;

        User u{
            .id = 42,
            .name = "Kirill",
            .active = true,
            .age = 30,
        };

        auto hset_res = co_await hset_struct(client, "user:42", u);
        if (!hset_res)
        {
            const auto& err = hset_res.error();
            error("redis_structs_example: hset_struct user:42 failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }
        info("redis_structs_example: hset_struct user:42 fields={}", hset_res.value());

        auto hget_res = co_await hget_struct<User>(client, "user:42");
        if (!hget_res)
        {
            const auto& err = hget_res.error();
            error("redis_structs_example: hget_struct user:42 failed, category={}, message={}",
                  static_cast<int>(err.category), err.message);
            co_return;
        }

        if (!hget_res.value().has_value())
        {
            warn("redis_structs_example: hget_struct user:42 -> (nil)");
        }
        else
        {
            const User& u2 = *hget_res.value();
            info("redis_structs_example: hget_struct user:42 -> id={} name='{}' active={} age={}",
                 u2.id,
                 u2.name,
                 u2.active,
                 u2.age.has_value() ? std::to_string(*u2.age) : std::string("<null>"));
        }
    }

    info("redis_structs_example: done");
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

    info("main: starting uvent");

    usub::Uvent uvent(4);
    system::co_spawn(redis_example());
    system::co_spawn(redis_structs_example());
    uvent.run();

    info("main: uvent stopped");
    return 0;
}