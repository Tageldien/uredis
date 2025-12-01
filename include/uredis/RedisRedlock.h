#ifndef UREDIS_REDISREDLOCK_H
#define UREDIS_REDISREDLOCK_H

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "uvent/Uvent.h"
#include "uredis/RedisClient.h"
#include "uredis/RedisTypes.h"

namespace usub::uredis
{
    namespace task   = usub::uvent::task;
    namespace system = usub::uvent::system;

    struct RedlockConfig
    {
        std::vector<RedisConfig> nodes;

        int ttl_ms{3000};

        int retry_count{3};

        int retry_delay_ms{200};

        int drift_factor_ppm{2000};
    };

    class RedisRedlock
    {
    public:
        struct LockHandle
        {
            std::string resource;
            std::string value;
            int ttl_ms{0};
        };

        explicit RedisRedlock(RedlockConfig cfg);

        RedisRedlock(std::vector<std::shared_ptr<RedisClient>> clients,
                     RedlockConfig cfg);

        task::Awaitable<RedisResult<void>> connect_all();

        task::Awaitable<RedisResult<LockHandle>> lock(std::string resource);

        task::Awaitable<RedisResult<void>> unlock(const LockHandle& handle);

    private:
        RedlockConfig cfg_;
        std::vector<std::shared_ptr<RedisClient>> clients_;

        task::Awaitable<void> unlock_all_nodes(
            std::string_view resource,
            std::string_view value);

        static std::string generate_random_value();
    };
} // namespace usub::uredis

#endif // UREDIS_REDISREDLOCK_H