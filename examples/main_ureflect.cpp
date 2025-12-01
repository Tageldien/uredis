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
        co_return;
    }

    const User& u2 = *loaded.value();
    info("reflect_example: hget_struct user:42 -> id={} name='{}' active={} age={}",
         u2.id,
         u2.name,
         u2.active,
         u2.age.has_value() ? std::to_string(*u2.age) : std::string("<null>"));

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
    system::co_spawn(reflect_example());
    uvent.run();

    info("main(reflect): uvent stopped");
    return 0;
}