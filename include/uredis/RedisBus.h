#ifndef UREDIS_REDISBUS_H
#define UREDIS_REDISBUS_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "uvent/Uvent.h"
#include "uvent/sync/AsyncMutex.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisSubscriber.h"
#include "uredis/RedisTypes.h"

namespace usub::uredis
{
    namespace task = usub::uvent::task;
    namespace sync = usub::uvent::sync;
    namespace system = usub::uvent::system;

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

    private:
        Config cfg_;

        std::shared_ptr<RedisClient> pub_client_;
        std::shared_ptr<RedisSubscriber> sub_client_;

        bool connected_{false};
        bool stopping_{false};

        std::unordered_map<std::string, Callback> desired_channels_;
        std::unordered_map<std::string, Callback> desired_patterns_;

        sync::AsyncMutex mutex_;

        task::Awaitable<RedisResult<void>> ensure_connected_locked();
        task::Awaitable<RedisResult<void>> resubscribe_all_locked();
        task::Awaitable<void> run_loop();

        RedisResult<void> apply_subscribe_locked(
            const std::string& channel,
            const Callback& cb);

        RedisResult<void> apply_psubscribe_locked(
            const std::string& pattern,
            const Callback& cb);

        RedisResult<void> apply_unsubscribe_locked(const std::string& channel);
        RedisResult<void> apply_punsubscribe_locked(const std::string& pattern);

        void notify_error(const RedisError& err) const;
        void notify_reconnect() const;
    };
} // namespace usub::uredis

#endif // UREDIS_REDISBUS_H