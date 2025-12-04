#ifndef REDISCLIENT_H
#define REDISCLIENT_H

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <array>
#include <unordered_map>
#include <atomic>

#include "uvent/Uvent.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/sync/AsyncMutex.h"
#include "uvent/utils/buffer/DynamicBuffer.h"

#include "uredis/RedisTypes.h"
#include "uredis/RespParser.h"

namespace usub::uredis
{
    namespace net = usub::uvent::net;
    namespace sync = usub::uvent::sync;
    namespace task = usub::uvent::task;
    namespace system = usub::uvent::system;

    struct RedisConfig
    {
        std::string host;
        std::uint16_t port{6379};
        int db{0};

        std::optional<std::string> username;
        std::optional<std::string> password;

        int connect_timeout_ms{5000};
        int io_timeout_ms{5000};
    };

    class RedisClient
    {
    public:
        explicit RedisClient(RedisConfig cfg);
        ~RedisClient();

        task::Awaitable<RedisResult<void>> connect();

        inline bool connected() const noexcept
        {
            return this->connected_;
        }

        task::Awaitable<RedisResult<RedisValue>> command(
            std::string_view cmd,
            std::span<const std::string_view> args);

        template <typename... Args>
        task::Awaitable<RedisResult<RedisValue>> command(
            std::string_view cmd,
            Args&&... args)
        {
            std::array<std::string_view, sizeof...(Args)> arr{
                std::string_view{std::forward<Args>(args)}...
            };
            co_return co_await this->command(
                cmd,
                std::span<const std::string_view>(arr.data(), arr.size()));
        }

        task::Awaitable<RedisResult<std::optional<std::string>>> get(std::string_view key);
        task::Awaitable<RedisResult<void>> set(std::string_view key, std::string_view value);
        task::Awaitable<RedisResult<void>> setex(std::string_view key, int ttl_sec, std::string_view value);
        task::Awaitable<RedisResult<int64_t>> del(std::span<const std::string_view> keys);
        task::Awaitable<RedisResult<int64_t>> incrby(std::string_view key, int64_t delta);

        task::Awaitable<RedisResult<int64_t>> hset(
            std::string_view key,
            std::string_view field,
            std::string_view value);

        task::Awaitable<RedisResult<std::optional<std::string>>> hget(
            std::string_view key,
            std::string_view field);

        task::Awaitable<RedisResult<std::unordered_map<std::string, std::string>>> hgetall(
            std::string_view key);

        task::Awaitable<RedisResult<int64_t>> sadd(
            std::string_view key,
            std::span<const std::string_view> members);

        task::Awaitable<RedisResult<int64_t>> srem(
            std::string_view key,
            std::span<const std::string_view> members);

        task::Awaitable<RedisResult<std::vector<std::string>>> smembers(
            std::string_view key);

        task::Awaitable<RedisResult<int64_t>> lpush(
            std::string_view key,
            std::span<const std::string_view> values);

        task::Awaitable<RedisResult<std::vector<std::string>>> lrange(
            std::string_view key,
            int64_t start,
            int64_t stop);

        task::Awaitable<RedisResult<int64_t>> zadd(
            std::string_view key,
            std::span<const std::pair<std::string, double>> members);

        task::Awaitable<RedisResult<std::vector<std::pair<std::string, double>>>> zrange_with_scores(
            std::string_view key,
            int64_t start,
            int64_t stop);

        const RedisConfig& config() const { return this->config_; }

    private:
        struct PendingRequest
        {
            sync::AsyncEvent event{sync::Reset::Manual, false};
            RedisResult<RedisValue> result{
                std::unexpected(RedisError{RedisErrorCategory::Protocol, "uninitialized"})
            };
        };

        RedisConfig config_{};
        net::TCPClientSocket socket_{};
        bool connected_{false};

        RespParser parser_{};

        sync::AsyncMutex pending_mutex_;
        std::deque<std::shared_ptr<PendingRequest>> pending_requests_;

        sync::AsyncMutex write_mutex_;
        bool reader_started_{false};
        bool closing_{false};
        std::atomic<bool> reader_stopped_{false};

        task::Awaitable<void> reader_loop();

        static std::vector<uint8_t> encode_command(
            std::string_view cmd,
            std::span<const std::string_view> args);

        task::Awaitable<void> fail_all_pending(
            RedisErrorCategory cat,
            std::string_view message);
    };
} // namespace usub::uredis

#endif //REDISCLIENT_H
