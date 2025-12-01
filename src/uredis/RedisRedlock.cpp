#include "uredis/RedisRedlock.h"

#include <array>
#include <cstdio>
#include <random>

#ifdef UREDIS_LOGS
#include <ulog/ulog.h>
#endif

namespace usub::uredis
{
    using Clock      = std::chrono::steady_clock;
    using Millis     = std::chrono::milliseconds;

    std::string RedisRedlock::generate_random_value()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uint64_t a = gen();
        std::uint64_t b = gen();

        char buf[33];
        std::snprintf(
            buf,
            sizeof(buf),
            "%016llx%016llx",
            static_cast<unsigned long long>(a),
            static_cast<unsigned long long>(b));

        return std::string(buf, 32);
    }

    RedisRedlock::RedisRedlock(RedlockConfig cfg)
        : cfg_(std::move(cfg))
    {
        this->clients_.reserve(this->cfg_.nodes.size());
        for (const auto& node_cfg : this->cfg_.nodes)
        {
            this->clients_.push_back(
                std::make_shared<RedisClient>(node_cfg));
        }
    }

    RedisRedlock::RedisRedlock(std::vector<std::shared_ptr<RedisClient>> clients,
                               RedlockConfig cfg)
        : cfg_(std::move(cfg))
        , clients_(std::move(clients))
    {
        this->cfg_.nodes.clear();
    }

    task::Awaitable<RedisResult<void>> RedisRedlock::connect_all()
    {
        if (this->clients_.empty())
        {
            RedisError err{
                RedisErrorCategory::Io,
                "RedisRedlock::connect_all: no nodes configured"
            };
#ifdef UREDIS_LOGS
            usub::ulog::error("{}", err.message);
#endif
            co_return std::unexpected(err);
        }

        for (auto& c : this->clients_)
        {
            if (!c) continue;

            auto res = co_await c->connect();
            if (!res)
            {
#ifdef UREDIS_LOGS
                const auto& err = res.error();
                usub::ulog::error(
                    "RedisRedlock::connect_all: connect failed: {}",
                    err.message);
#endif
                co_return std::unexpected(res.error());
            }
        }

#ifdef UREDIS_LOGS
        usub::ulog::info("RedisRedlock::connect_all: all nodes connected");
#endif
        co_return RedisResult<void>{};
    }

    task::Awaitable<void> RedisRedlock::unlock_all_nodes(
        std::string_view resource,
        std::string_view value)
    {
        static const std::string script =
            "if redis.call('GET', KEYS[1]) == ARGV[1] then "
            "return redis.call('DEL', KEYS[1]) "
            "else return 0 end";

        for (auto& client : this->clients_)
        {
            if (!client) continue;

            std::string res_key(resource);
            std::string val_str(value);
            std::string num_keys = "1";

            std::array<std::string_view, 4> args{
                std::string_view{script},
                std::string_view{num_keys},
                std::string_view{res_key},
                std::string_view{val_str}
            };

            auto resp = co_await client->command(
                "EVAL",
                std::span<const std::string_view>(args.data(), args.size()));

            (void)resp;

#ifdef UREDIS_LOGS
            if (!resp)
            {
                const auto& err = resp.error();
                usub::ulog::warn(
                    "RedisRedlock::unlock_all_nodes: EVAL failed for key='{}': {}",
                    res_key,
                    err.message);
            }
#endif
        }

        co_return;
    }

    task::Awaitable<RedisResult<RedisRedlock::LockHandle>> RedisRedlock::lock(
        std::string resource)
    {
        if (this->clients_.empty())
        {
            RedisError err{
                RedisErrorCategory::Io,
                "RedisRedlock::lock: no nodes configured"
            };
#ifdef UREDIS_LOGS
            usub::ulog::error("{}", err.message);
#endif
            co_return std::unexpected(err);
        }

        const std::size_t total_nodes = this->clients_.size();
        const std::size_t quorum      = total_nodes / 2 + 1;

        if (quorum == 0)
        {
            RedisError err{
                RedisErrorCategory::Protocol,
                "RedisRedlock::lock: invalid quorum"
            };
#ifdef UREDIS_LOGS
            usub::ulog::error("{}", err.message);
#endif
            co_return std::unexpected(err);
        }

        const int ttl_ms      = this->cfg_.ttl_ms;
        const int retry_count = this->cfg_.retry_count;
        const int retry_delay = this->cfg_.retry_delay_ms;

        for (int attempt = 0; attempt < retry_count; ++attempt)
        {
            const auto start = Clock::now();

            std::string token = generate_random_value();
            std::size_t success_count = 0;

#ifdef UREDIS_LOGS
            usub::ulog::debug(
                "RedisRedlock::lock: attempt={} resource='{}' token={}",
                attempt,
                resource,
                token);
#endif

            for (auto& client : this->clients_)
            {
                if (!client) continue;

                std::string key     = resource;
                std::string value   = token;
                std::string ttl_str = std::to_string(ttl_ms);
                static constexpr std::string_view px_sv{"PX"};

                std::array<std::string_view, 4> args{
                    std::string_view{key},
                    std::string_view{value},
                    px_sv,
                    std::string_view{ttl_str}
                };

                auto resp = co_await client->command(
                    "SET",
                    std::span<const std::string_view>(args.data(), args.size()));

                if (!resp)
                {
#ifdef UREDIS_LOGS
                    const auto& err = resp.error();
                    usub::ulog::debug(
                        "RedisRedlock::lock: SET failed on node: {}",
                        err.message);
#endif
                    continue;
                }

                const RedisValue& v = *resp;
                if (v.is_simple_string() && v.as_string() == "OK")
                {
                    ++success_count;
                }
                else
                {
#ifdef UREDIS_LOGS
                    usub::ulog::debug(
                        "RedisRedlock::lock: SET reply not OK on node");
#endif
                }
            }

            const auto end      = Clock::now();
            const auto elapsed  = std::chrono::duration_cast<Millis>(end - start).count();
            const int   validity_ms = ttl_ms - static_cast<int>(elapsed);

#ifdef UREDIS_LOGS
            usub::ulog::debug(
                "RedisRedlock::lock: attempt={} success_count={} quorum={} elapsed_ms={} validity_ms={}",
                attempt,
                success_count,
                quorum,
                elapsed,
                validity_ms);
#endif

            if (success_count >= quorum && validity_ms > 0)
            {
                LockHandle handle{
                    .resource = std::move(resource),
                    .value    = std::move(token),
                    .ttl_ms   = validity_ms
                };

#ifdef UREDIS_LOGS
                usub::ulog::info(
                    "RedisRedlock::lock: acquired lock resource='{}' token={} validity_ms={}",
                    handle.resource,
                    handle.value,
                    handle.ttl_ms);
#endif
                co_return handle;
            }

            co_await this->unlock_all_nodes(resource, token);

            if (attempt + 1 < retry_count)
            {
#ifdef UREDIS_LOGS
                usub::ulog::info(
                    "RedisRedlock::lock: attempt {} failed, retry in {} ms",
                    attempt,
                    retry_delay);
#endif
                co_await system::this_coroutine::sleep_for(Millis(retry_delay));
            }
        }

        RedisError err{
            RedisErrorCategory::Io,
            "RedisRedlock::lock: unable to acquire lock"
        };
#ifdef UREDIS_LOGS
        usub::ulog::error("{}", err.message);
#endif
        co_return std::unexpected(err);
    }

    task::Awaitable<RedisResult<void>> RedisRedlock::unlock(
        const LockHandle& handle)
    {
#ifdef UREDIS_LOGS
        usub::ulog::info(
            "RedisRedlock::unlock: resource='{}' token={}",
            handle.resource,
            handle.value);
#endif

        co_await this->unlock_all_nodes(handle.resource, handle.value);
        co_return RedisResult<void>{};
    }
} // namespace usub::uredis
